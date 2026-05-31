// voice.c — microphone capture + playback + UDP relay.
// Kept SEPARATE from main.c so miniaudio's windows.h never clashes with raylib.

#include "voice.h"

// IMPORTANT: winsock2.h MUST be included before windows.h. miniaudio includes
// windows.h, so we include the socket headers FIRST, then miniaudio.
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

static char  s_vhost[128] = "127.0.0.1";
static SOCKET s_sock = INVALID_SOCKET;
static struct sockaddr_in s_srv;
static volatile int s_talking = 0;

static unsigned char s_ring[RING_BYTES];
static volatile int s_head = 0, s_tail = 0;
static ma_device s_mic, s_spk;
static int s_micOK = 0, s_spkOK = 0;

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

static void on_mic(ma_device *dev, void *out, const void *in, ma_uint32 frames) {
    (void)dev; (void)out;
    if (s_talking && s_sock != INVALID_SOCKET) {
        int bytes = (int)frames * 2;
        sendto(s_sock, (const char *)in, bytes, 0,
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

int voice_init(void) {
    load_host();
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

    char hi = 0;
    sendto(s_sock, &hi, 1, 0, (struct sockaddr*)&s_srv, sizeof(s_srv));

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

void voice_poll(void) {
    if (s_sock == INVALID_SOCKET) return;
    unsigned char buf[2048];
    for (int i = 0; i < 16; i++) {
        int n = recvfrom(s_sock, (char *)buf, sizeof(buf), 0, NULL, NULL);
        if (n <= 0) break;
        ring_push(buf, n);
    }
}
