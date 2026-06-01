// voice.c — microphone capture + playback + UDP relay, with per-person volume.
// Kept SEPARATE from main.c so miniaudio's windows.h never clashes with raylib.
//
// Packet format: first 16 bytes = sender name (null-padded), rest = 16-bit PCM.
// On playback we read the sender, apply that peer's mute + volume and the master
// volume, then mix into the playback ring buffer.

#include "voice.h"

// winsock2.h MUST come before windows.h (which miniaudio includes).
#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define CLOSESOCK closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  typedef int SOCKET;
  #define INVALID_SOCKET (-1)
  #define CLOSESOCK close
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define VOICE_FILE    "desk_voice.txt"
#define VOICE_PORT    8081
#define SAMPLE_RATE   16000
#define RING_BYTES    (SAMPLE_RATE * 2 * 2)   // 2 seconds
#define NAME_LEN      16                       // fixed-size name header

static char  s_vhost[128] = "127.0.0.1";
static char  s_myname[NAME_LEN] = "";          // our own name (sent in packets)
static SOCKET s_sock = INVALID_SOCKET;
static struct sockaddr_in s_srv;
static volatile int s_talking = 0;

static unsigned char s_ring[RING_BYTES];
static volatile int s_head = 0, s_tail = 0;
static ma_device s_mic, s_spk;
static int s_micOK = 0, s_spkOK = 0;

// Master + per-peer volume/mute tables.
static volatile int s_master = 100;            // 0..200
#define MAX_PEERS 64
static char s_peerName[MAX_PEERS][NAME_LEN];
static int  s_peerVol[MAX_PEERS];              // 0..200
static int  s_peerMute[MAX_PEERS];
static int  s_peerCount = 0;

static int peer_index(const char *name) {       // find or create a peer slot
    for (int i = 0; i < s_peerCount; i++)
        if (strncmp(s_peerName[i], name, NAME_LEN) == 0) return i;
    if (s_peerCount >= MAX_PEERS) return -1;
    int i = s_peerCount++;
    strncpy(s_peerName[i], name, NAME_LEN-1); s_peerName[i][NAME_LEN-1]='\0';
    s_peerVol[i] = 100; s_peerMute[i] = 0;
    return i;
}

static void ring_push(const unsigned char *d, int n) {
    for (int i = 0; i < n; i++) {
        int nt = (s_tail + 1) % RING_BYTES;
        if (nt == s_head) break;
        s_ring[s_tail] = d[i];
        s_tail = nt;
    }
}
static int ring_pop(unsigned char *o, int want) {
    int got = 0;
    while (got < want && s_head != s_tail) {
        o[got++] = s_ring[s_head];
        s_head = (s_head + 1) % RING_BYTES;
    }
    return got;
}

// Mic callback: prepend our name, send name+audio.
static void on_mic(ma_device *dev, void *out, const void *in, ma_uint32 frames) {
    (void)dev; (void)out;
    if (s_talking && s_sock != INVALID_SOCKET) {
        int bytes = (int)frames * 2;
        unsigned char pkt[NAME_LEN + 4096];
        if (bytes > (int)sizeof(pkt) - NAME_LEN) bytes = sizeof(pkt) - NAME_LEN;
        memset(pkt, 0, NAME_LEN);
        memcpy(pkt, s_myname, strnlen(s_myname, NAME_LEN));
        memcpy(pkt + NAME_LEN, in, bytes);
        sendto(s_sock, (const char *)pkt, NAME_LEN + bytes, 0,
               (struct sockaddr *)&s_srv, sizeof(s_srv));
    }
}
static void on_spk(ma_device *dev, void *out, const void *in, ma_uint32 frames) {
    (void)dev; (void)in;
    int want = (int)frames * 2;
    int got = ring_pop((unsigned char *)out, want);
    if (got < want) memset((unsigned char *)out + got, 0, want - got);
}

static void load_host(void) {
    FILE *fp = fopen(VOICE_FILE, "r");
    if (!fp) return;
    if (fgets(s_vhost, sizeof(s_vhost), fp)) {
        size_t n = strlen(s_vhost);
        while (n>0 && (s_vhost[n-1]=='\n'||s_vhost[n-1]=='\r'||s_vhost[n-1]==' ')) s_vhost[--n]='\0';
    }
    fclose(fp);
}

int voice_init(const char *myName) {
    load_host();
    if (myName) { strncpy(s_myname, myName, NAME_LEN-1); s_myname[NAME_LEN-1]='\0'; }
#if defined(_WIN32)
    WSADATA w; if (WSAStartup(MAKEWORD(2,2), &w) != 0) return 0;
#endif
    s_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sock == INVALID_SOCKET) return 0;
#if defined(_WIN32)
    u_long nb = 1; ioctlsocket(s_sock, FIONBIO, &nb);
#else
    int fl = fcntl(s_sock, F_GETFL, 0); fcntl(s_sock, F_SETFL, fl | O_NONBLOCK);
#endif
    memset(&s_srv, 0, sizeof(s_srv));
    s_srv.sin_family = AF_INET;
    s_srv.sin_port = htons(VOICE_PORT);
    s_srv.sin_addr.s_addr = inet_addr(s_vhost);

    unsigned char hi[NAME_LEN]; memset(hi,0,NAME_LEN);
    memcpy(hi, s_myname, strnlen(s_myname, NAME_LEN));
    sendto(s_sock, (const char*)hi, NAME_LEN, 0, (struct sockaddr*)&s_srv, sizeof(s_srv));

    ma_device_config mc = ma_device_config_init(ma_device_type_capture);
    mc.capture.format = ma_format_s16; mc.capture.channels = 1;
    mc.sampleRate = SAMPLE_RATE; mc.dataCallback = on_mic;
    if (ma_device_init(NULL, &mc, &s_mic) == MA_SUCCESS) { ma_device_start(&s_mic); s_micOK = 1; }

    ma_device_config pc = ma_device_config_init(ma_device_type_playback);
    pc.playback.format = ma_format_s16; pc.playback.channels = 1;
    pc.sampleRate = SAMPLE_RATE; pc.dataCallback = on_spk;
    if (ma_device_init(NULL, &pc, &s_spk) == MA_SUCCESS) { ma_device_start(&s_spk); s_spkOK = 1; }

    return (s_micOK || s_spkOK) ? 1 : 0;
}

void voice_shutdown(void) {
    if (s_micOK) ma_device_uninit(&s_mic);
    if (s_spkOK) ma_device_uninit(&s_spk);
    if (s_sock != INVALID_SOCKET) CLOSESOCK(s_sock);
#if defined(_WIN32)
    WSACleanup();
#endif
}

void voice_set_talking(int on) { s_talking = on ? 1 : 0; }
int  voice_is_talking(void)    { return s_talking; }

void voice_set_master(int v) { if (v<0) v=0; if (v>200) v=200; s_master = v; }
void voice_set_peer_volume(const char *name, int v) {
    if (v<0) v=0; if (v>200) v=200;
    int i = peer_index(name); if (i>=0) s_peerVol[i] = v;
}
void voice_set_peer_mute(const char *name, int muted) {
    int i = peer_index(name); if (i>=0) s_peerMute[i] = muted?1:0;
}
int voice_get_peer_volume(const char *name) {
    for (int i=0;i<s_peerCount;i++) if (strncmp(s_peerName[i],name,NAME_LEN)==0) return s_peerVol[i];
    return 100;
}
int voice_get_peer_mute(const char *name) {
    for (int i=0;i<s_peerCount;i++) if (strncmp(s_peerName[i],name,NAME_LEN)==0) return s_peerMute[i];
    return 0;
}

void voice_poll(void) {
    if (s_sock == INVALID_SOCKET) return;
    unsigned char buf[NAME_LEN + 4096];
    for (int i = 0; i < 16; i++) {
        int n = recvfrom(s_sock, (char *)buf, sizeof(buf), 0, NULL, NULL);
        if (n <= NAME_LEN) continue;            // need header + some audio

        char sender[NAME_LEN]; memcpy(sender, buf, NAME_LEN); sender[NAME_LEN-1]='\0';
        int idx = peer_index(sender);
        int vol = (idx>=0) ? s_peerVol[idx] : 100;
        int mute = (idx>=0) ? s_peerMute[idx] : 0;
        if (mute) continue;                     // drop this person's audio entirely

        // Effective gain = peer volume * master, both as percentages.
        float gain = (vol / 100.0f) * (s_master / 100.0f);

        int samples = (n - NAME_LEN) / 2;
        short *pcm = (short *)(buf + NAME_LEN);
        for (int s = 0; s < samples; s++) {
            int v = (int)(pcm[s] * gain);
            if (v > 32767) v = 32767; else if (v < -32768) v = -32768;  // clip
            pcm[s] = (short)v;
        }
        ring_push((unsigned char *)pcm, samples * 2);
    }
}
