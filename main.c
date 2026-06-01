// main.c — Black desktop: folder icon, Update button, and a Lobby room.
// Cross-platform: macOS + Windows. Built with raylib.
//
// Screens:
//   USERNAME: first run — type a name (saved locally, no network).
//   DESKTOP : black screen, a folder (top-left), an Update button + a "Lobby"
//             entry icon (bottom-right). Opens in a window; F11 = fullscreen.
//   LOBBY   : white room with a floating red cube. ESC/Back returns to desktop.
//
// Networking is NOT wired up yet — the lobby is a local mockup. The server and
// connection come next.

#include "raylib.h"
#include "rlgl.h"      // rlPushMatrix / rlRotatef for the 3D background
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

// Voice (mic/speaker/UDP) lives in voice.c behind this small API, so its
// windows.h include never clashes with raylib.
#include "voice.h"
#include "game.h"

// ---- Crisp anti-aliased text (replaces raylib's blocky default font) ----
static Font g_font;
static bool g_fontOK = false;

static void LoadUIFont(void) {
    const char *candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",        // Windows
        "C:/Windows/Fonts/arial.ttf",
        "/System/Library/Fonts/SFNS.ttf",       // macOS
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        if (FileExists(candidates[i])) {
            // Load at high resolution so it stays sharp at any size.
            g_font = LoadFontEx(candidates[i], 64, NULL, 0);
            if (g_font.texture.id != 0) {
                SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
                g_fontOK = true;
                return;
            }
        }
    }
}

// Drop-in replacements with the same argument order as DrawText/MeasureText.
static void Txt(const char *t, int x, int y, int size, Color c) {
    if (g_fontOK) DrawTextEx(g_font, t, (Vector2){(float)x,(float)y}, (float)size, 1.0f, c);
    else          DrawText(t, x, y, size, c);
}
static int TxtW(const char *t, int size) {
    if (g_fontOK) return (int)MeasureTextEx(g_font, t, (float)size, 1.0f).x;
    return MeasureText(t, size);
}

// A flat, minimal, hover-aware button. No drop shadow, slight rounding, a thin
// border that brightens on hover — a more restrained, mature look.
static bool Button(Rectangle r, const char *label, int fontSize,
                   Color base, Color hover, Color textCol, Vector2 mouse) {
    bool over = CheckCollisionPointRec(mouse, r);
    Color c = over ? hover : base;
    DrawRectangleRounded(r, 0.16f, 6, c);
    DrawRectangleRoundedLines(r, 0.16f, 6, over ? (Color){120,125,140,200} : (Color){70,72,84,160});
    int tw = TxtW(label, fontSize);
    Txt(label, (int)(r.x + r.width/2 - tw/2), (int)(r.y + r.height/2 - fontSize/2), fontSize, textCol);
    return over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Horizontal slider 0..maxVal. Drag or click the track to set. Returns value.
static int Slider(Rectangle r, int value, int maxVal, Vector2 mouse) {
    DrawRectangleRounded((Rectangle){r.x, r.y + r.height/2 - 3, r.width, 6}, 1.0f, 4, (Color){55,57,68,255});
    float frac = (float)value / (float)maxVal;
    float kx = r.x + frac * r.width;
    // filled portion
    DrawRectangleRounded((Rectangle){r.x, r.y + r.height/2 - 3, kx - r.x, 6}, 1.0f, 4, (Color){90,120,170,255});
    DrawCircle((int)kx, (int)(r.y + r.height/2), 8, (Color){150,170,210,255});
    if (CheckCollisionPointRec(mouse, (Rectangle){r.x-8, r.y-6, r.width+16, r.height+12})
        && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float f = (mouse.x - r.x) / r.width;
        if (f < 0) f = 0; if (f > 1) f = 1;
        value = (int)(f * maxVal + 0.5f);
    }
    return value;
}

// ---- Bump this every time you publish a new GitHub release ----
#define APP_VERSION "v0.1.0"
#define GH_OWNER    "everydaymighty"
#define GH_REPO     "desk"

typedef enum { SCREEN_USERNAME, SCREEN_DESKTOP, SCREEN_LOBBY, SCREEN_GAME } Screen;

static char g_updateMsg[256] = "";

// ---- Username, saved locally next to the exe (no network) ----
#define USER_FILE "desk_user.txt"
static char g_username[32] = "";

// ---- Lobby networking (HTTP polling) ----
// Server base URL is read from desk_server.txt (e.g. http://localhost:8080
// for local testing, or your ngrok https URL for other computers). If the file
// is missing we default to localhost.
#define SERVER_FILE "desk_server.txt"
#define AUTH_FILE   "desk_auth.txt"     // auth server URL (default localhost:8090)
#define ONLINE_FILE "desk_online.txt"
#define CHAT_FILE   "desk_chat.txt"
#define FRIENDS_FILE "desk_friends.txt"
static char g_serverURL[256] = "http://localhost:8080";
static char g_authURL[256]   = "http://localhost:8090";
static char g_token[512]     = "";   // session token from the auth server
static char g_online[2048]   = "";   // raw newline-separated names from server
static char g_chat[4096]     = "";   // raw newline-separated chat lines from server
static char g_friends[2048]  = "";   // raw newline-separated friend names
#define GAMESTATE_FILE "desk_game.txt"
#define LEADERBOARD_FILE "desk_lb.txt"
static char g_gamestate[512] = "";   // raw JSON match state from server
static char g_leaderboard[1024] = "";// raw "name W-L" lines
static int  g_myRole = -1;           // -1 unknown, 0/1 = player slot, 2 = spectator

// Forward declaration (defined later, used by GameJoin above its definition).
static int RunCapture(const char *cmd, char *out, size_t outSize);

static void LoadAuthURL(void)
{
    FILE *fp = fopen(AUTH_FILE, "r");
    if (!fp) return;
    if (fgets(g_authURL, sizeof(g_authURL), fp)) {
        size_t n = strlen(g_authURL);
        while (n > 0 && (g_authURL[n-1]=='\n'||g_authURL[n-1]=='\r'||g_authURL[n-1]==' '||g_authURL[n-1]=='/'))
            g_authURL[--n] = '\0';
    }
    fclose(fp);
}

static void LoadServerURL(void)
{
    FILE *fp = fopen(SERVER_FILE, "r");
    if (!fp) return;
    if (fgets(g_serverURL, sizeof(g_serverURL), fp)) {
        size_t n = strlen(g_serverURL);
        while (n > 0 && (g_serverURL[n-1]=='\n'||g_serverURL[n-1]=='\r'||g_serverURL[n-1]==' '||g_serverURL[n-1]=='/'))
            g_serverURL[--n] = '\0';
    }
    fclose(fp);
}

// Fire a heartbeat (/hello) and refresh the online list (/online -> file),
// both detached so they never block the window. Called on a timer.
static void PollLobby(void)
{
    char cmd[1024];
#if defined(_WIN32)
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"\"%s/hello?name=%s\" >NUL 2>&1",
        g_serverURL, g_username);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"-o \"%s\" \"%s/online\" 2>NUL",
        ONLINE_FILE, g_serverURL);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"-o \"%s\" \"%s/chat\" 2>NUL",
        CHAT_FILE, g_serverURL);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"-o \"%s\" \"%s/friends?name=%s\" 2>NUL",
        FRIENDS_FILE, g_serverURL, g_username);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"-o \"%s\" \"%s/leaderboard\" 2>NUL",
        LEADERBOARD_FILE, g_serverURL);
    system(cmd);
#else
    snprintf(cmd, sizeof(cmd),
        "( curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"\"%s/hello?name=%s\" >/dev/null 2>&1; "
        "  curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"-o \"%s\" \"%s/online\" 2>/dev/null; "
        "  curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"-o \"%s\" \"%s/chat\" 2>/dev/null; "
        "  curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"-o \"%s\" \"%s/friends?name=%s\" 2>/dev/null; "
        "  curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"-o \"%s\" \"%s/leaderboard\" 2>/dev/null ) &",
        g_serverURL, g_username, ONLINE_FILE, g_serverURL, CHAT_FILE, g_serverURL,
        FRIENDS_FILE, g_serverURL, g_username, LEADERBOARD_FILE, g_serverURL);
    system(cmd);
#endif
}

static void ReadOnlineFile(void)
{
    FILE *fp = fopen(ONLINE_FILE, "r");
    if (fp) {
        size_t n = fread(g_online, 1, sizeof(g_online) - 1, fp);
        g_online[n] = '\0';
        fclose(fp);
    }
    fp = fopen(CHAT_FILE, "r");
    if (fp) {
        size_t n = fread(g_chat, 1, sizeof(g_chat) - 1, fp);
        g_chat[n] = '\0';
        fclose(fp);
    }
    fp = fopen(FRIENDS_FILE, "r");
    if (fp) {
        size_t n = fread(g_friends, 1, sizeof(g_friends) - 1, fp);
        g_friends[n] = '\0';
        fclose(fp);
    }
    fp = fopen(LEADERBOARD_FILE, "r");
    if (fp) {
        size_t n = fread(g_leaderboard, 1, sizeof(g_leaderboard) - 1, fp);
        g_leaderboard[n] = '\0';
        fclose(fp);
    }
}

// Tell the server to add a friend (detached, non-blocking).
static void AddFriend(const char *friendName)
{
    if (!friendName[0]) return;
    char cmd[1024];
#if defined(_WIN32)
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"\"%s/addfriend?name=%s&friend=%s\" >NUL 2>&1",
        g_serverURL, g_username, friendName);
#else
    snprintf(cmd, sizeof(cmd),
        "curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"\"%s/addfriend?name=%s&friend=%s\" >/dev/null 2>&1 &",
        g_serverURL, g_username, friendName);
#endif
    system(cmd);
}

// Is this name already in our friends list?
static int IsFriend(const char *name)
{
    char buf[2048]; strncpy(buf, g_friends, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
    char *ln = strtok(buf, "\n");
    while (ln) { if (strcmp(ln, name) == 0) return 1; ln = strtok(NULL, "\n"); }
    return 0;
}

// --- Game matchmaking helpers ---
// Claim a slot (or spectator). Reads back "PLAYER 0/1" or "SPECTATOR".
static void GameJoin(void)
{
    char cmd[1024], out[64];
    snprintf(cmd, sizeof(cmd), "curl -s -m 5 -H \"ngrok-skip-browser-warning: 1\" \"%s/join?name=%s\"", g_serverURL, g_username);
    if (RunCapture(cmd, out, sizeof(out))) {
        if (strncmp(out, "PLAYER 0", 8) == 0) g_myRole = 0;
        else if (strncmp(out, "PLAYER 1", 8) == 0) g_myRole = 1;
        else if (strncmp(out, "SPECTATOR", 9) == 0) g_myRole = 2;
    }
}
// Fetch current match state JSON into g_gamestate (detached, non-blocking).
static void GamePoll(void)
{
    char cmd[1024];
#if defined(_WIN32)
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"-o \"%s\" \"%s/gamestate\" 2>NUL", GAMESTATE_FILE, g_serverURL);
#else
    snprintf(cmd, sizeof(cmd),
        "curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"-o \"%s\" \"%s/gamestate\" 2>/dev/null &", GAMESTATE_FILE, g_serverURL);
#endif
    system(cmd);
    FILE *fp = fopen(GAMESTATE_FILE, "r");
    if (fp) { size_t n = fread(g_gamestate,1,sizeof(g_gamestate)-1,fp); g_gamestate[n]='\0'; fclose(fp); }
}
// Tiny JSON int extractor for the gamestate.
static int GsInt(const char *key) {
    char pat[32]; snprintf(pat,sizeof(pat),"\"%s\":",key);
    const char *p = strstr(g_gamestate, pat);
    if (!p) return 0;
    return atoi(p + strlen(pat));
}
static void GsStr(const char *key, char *out, size_t sz) {
    out[0]='\0';
    char pat[32]; snprintf(pat,sizeof(pat),"\"%s\":\"",key);
    const char *p = strstr(g_gamestate, pat);
    if (!p) return;
    p += strlen(pat); size_t o=0;
    while (*p && *p!='"' && o<sz-1) out[o++]=*p++;
    out[o]='\0';
}

// URL-encode a chat message (spaces and specials) into out.
static void UrlEncode(const char *in, char *out, size_t outSize)
{
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char*)in; *p && o + 4 < outSize; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p=='-' || *p=='_' || *p=='.' || *p=='~') {
            out[o++] = *p;
        } else {
            out[o++] = '%'; out[o++] = hex[*p >> 4]; out[o++] = hex[*p & 15];
        }
    }
    out[o] = '\0';
}

// Send a chat message to the server (detached, non-blocking).
static void SendChat(const char *msg)
{
    if (!msg[0]) return;
    char enc[640]; UrlEncode(msg, enc, sizeof(enc));
    char cmd[1024];
#if defined(_WIN32)
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"\"%s/say?name=%s&msg=%s\" >NUL 2>&1",
        g_serverURL, g_username, enc);
#else
    snprintf(cmd, sizeof(cmd),
        "curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"\"%s/say?name=%s&msg=%s\" >/dev/null 2>&1 &",
        g_serverURL, g_username, enc);
#endif
    system(cmd);
}


static int LoadUsername(void)
{
    FILE *fp = fopen(USER_FILE, "r");
    if (!fp) return 0;
    if (fgets(g_username, sizeof(g_username), fp) == NULL) g_username[0] = '\0';
    fclose(fp);
    size_t n = strlen(g_username);
    while (n > 0 && (g_username[n-1] == '\n' || g_username[n-1] == '\r' || g_username[n-1] == ' '))
        g_username[--n] = '\0';
    return g_username[0] != '\0';
}

static void SaveUsername(void)
{
    FILE *fp = fopen(USER_FILE, "w");
    if (!fp) return;
    fprintf(fp, "%s\n", g_username);
    fclose(fp);
}

// ---- Folder icon ----
static void DrawFolder(Rectangle box, Color body, Color tab)
{
    Rectangle tabRect = { box.x, box.y, box.width * 0.45f, box.height * 0.22f };
    DrawRectangleRounded(tabRect, 0.35f, 6, tab);
    Rectangle bodyRect = { box.x, box.y + box.height * 0.14f, box.width, box.height * 0.78f };
    DrawRectangleRounded(bodyRect, 0.12f, 6, body);
}

static int RunCapture(const char *cmd, char *out, size_t outSize)
{
    out[0] = '\0';
#if defined(_WIN32)
    FILE *fp = _popen(cmd, "r");
#else
    FILE *fp = popen(cmd, "r");
#endif
    if (!fp) return 0;
    if (fgets(out, (int)outSize, fp) == NULL) out[0] = '\0';
    size_t n = strlen(out);
    while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = '\0';
#if defined(_WIN32)
    _pclose(fp);
#else
    pclose(fp);
#endif
    return 1;
}

// Minimal JSON string-field extractor: finds "key":"value" and copies value.
static int JsonGetStr(const char *json, const char *key, char *out, size_t outSize)
{
    char pat[64]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p = strchr(p + strlen(pat), ':'); if (!p) return 0; p++;
    while (*p == ' ') p++;
    if (*p != '"') return 0; p++;
    size_t o = 0;
    while (*p && *p != '"' && o < outSize - 1) out[o++] = *p++;
    out[o] = '\0';
    return 1;
}
static int JsonIsTrue(const char *json, const char *key)
{
    char pat[64]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p = strchr(p + strlen(pat), ':'); if (!p) return 0;
    p++;
    while (*p == ' ') p++;
    return strncmp(p, "true", 4) == 0;
}

// Call /login or /register on the AUTH server using a POST body so the password
// never appears in the URL or process args. Writes the password to a temp file
// and has curl send it, then deletes it. On success, stores the session token.
static int AuthRequest(const char *action, const char *name, const char *pw,
                       char *errOut, size_t errSize)
{
    char cmd[1024], out[1024];
    // The ngrok-skip-browser-warning header bypasses ngrok's free interstitial
    // HTML page (which otherwise breaks the JSON response). Harmless off ngrok.
#if defined(_WIN32)
    snprintf(cmd, sizeof(cmd),
        "curl -s -m 8 -X POST -H \"Content-Type: application/json\" "
        "-H \"ngrok-skip-browser-warning: 1\" "
        "-d \"{\\\"name\\\":\\\"%s\\\",\\\"pw\\\":\\\"%s\\\"}\" \"%s/%s\"",
        name, pw, g_authURL, action);
#else
    snprintf(cmd, sizeof(cmd),
        "curl -s -m 8 -X POST -H 'Content-Type: application/json' "
        "-H 'ngrok-skip-browser-warning: 1' "
        "-d '{\"name\":\"%s\",\"pw\":\"%s\"}' '%s/%s'",
        name, pw, g_authURL, action);
#endif
    int ok = RunCapture(cmd, out, sizeof(out));

    if (!ok || out[0] == '\0') {
        snprintf(errOut, errSize, "No response (is the auth server running?)");
        return 0;
    }
    if (JsonIsTrue(out, "ok")) {
        JsonGetStr(out, "token", g_token, sizeof(g_token));
        return 1;
    }
    char reason[200] = "login failed";
    JsonGetStr(out, "err", reason, sizeof(reason));
    snprintf(errOut, errSize, "%s", reason);
    return 0;
}

#if defined(_WIN32)
  #define ASSET_NAME "black.exe"
#else
  #define ASSET_NAME "black"
#endif

static void DoInAppUpdate(const char *latest)
{
    const char *dlBase =
        "https://github.com/" GH_OWNER "/" GH_REPO "/releases/latest/download/";
#if defined(_WIN32)
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "powershell -NoProfile -Command "
        "\"Invoke-WebRequest -UseBasicParsing -Uri '%s%s' -OutFile 'black.new.exe'\"",
        dlBase, ASSET_NAME);
    char out[256];
    RunCapture(cmd, out, sizeof(out));
    system("powershell -NoProfile -WindowStyle Hidden -Command "
           "\"Start-Process powershell -WindowStyle Hidden -ArgumentList "
           "'-NoProfile','-Command','Start-Sleep -Seconds 2; "
           "Move-Item -Force black.new.exe black.exe; Start-Process black.exe'\"");
    snprintf(g_updateMsg, sizeof(g_updateMsg),
             "Updating to %s... the app will relaunch.", latest);
#else
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "curl -fsSL -H 'User-Agent: desk' -o black.new '%s%s' && chmod +x black.new",
        dlBase, ASSET_NAME);
    char out[256];
    RunCapture(cmd, out, sizeof(out));
    system("( sleep 2; mv -f black.new black; ./black ) >/dev/null 2>&1 &");
    snprintf(g_updateMsg, sizeof(g_updateMsg),
             "Updating to %s... the app will relaunch.", latest);
#endif
}

static bool g_quitForUpdate = false;

static void CheckForUpdate(void)
{
    char latest[128] = "";
    const char *api =
        "https://api.github.com/repos/" GH_OWNER "/" GH_REPO "/releases/latest";
    char cmd[1024];
#if defined(_WIN32)
    snprintf(cmd, sizeof(cmd),
        "powershell -NoProfile -Command "
        "\"try { (Invoke-RestMethod -Uri '%s' -Headers @{ 'User-Agent'='desk' }).tag_name } catch { '' }\"",
        api);
#else
    snprintf(cmd, sizeof(cmd),
        "curl -fsSL -H 'User-Agent: desk' %s "
        "| grep -m1 '\"tag_name\"' "
        "| sed -E 's/.*\"tag_name\": *\"([^\"]+)\".*/\\1/'",
        api);
#endif
    if (!RunCapture(cmd, latest, sizeof(latest)) || latest[0] == '\0') {
        snprintf(g_updateMsg, sizeof(g_updateMsg),
                 "No release published yet (this is normal).");
        return;
    }
    if (strcmp(latest, APP_VERSION) == 0) {
        snprintf(g_updateMsg, sizeof(g_updateMsg), "Up to date (%s).", APP_VERSION);
    } else {
        DoInAppUpdate(latest);
        g_quitForUpdate = true;
    }
}

int main(void)
{
    // Open a small window first so we can read the monitor's native resolution,
    // then resize to fill it. The user can change resolution later in Settings.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);  // 4x antialiasing
    InitWindow(1280, 720, "Desktop");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);   // don't let ESC close the app; we use it for navigation
    LoadUIFont();

    // Detect native resolution and start at it (windowed, centered).
    int mon = GetCurrentMonitor();
    int nativeW = GetMonitorWidth(mon), nativeH = GetMonitorHeight(mon);
    if (nativeW < 640 || nativeH < 480) { nativeW = 1280; nativeH = 720; } // fallback
    // Preset resolutions to cycle in Settings (last = Native).
    int resW[5] = { 1280, 1600, 1920, 2560, nativeW };
    int resH[5] = {  720,  900, 1080, 1440, nativeH };
    const char *resLabel[5] = { "1280x720", "1600x900", "1920x1080", "2560x1440", "Native" };
    int resMode = 4;  // default to Native
    SetWindowSize(nativeW, nativeH);
    SetWindowPosition( (GetMonitorWidth(mon)-nativeW)/2, (GetMonitorHeight(mon)-nativeH)/2 );

    // Always start at the login screen. We pre-fill the last username for
    // convenience, but never store the password, so the user logs in each run.
    LoadUsername();
    LoadAuthURL();
    Screen screen = SCREEN_USERNAME;
    LoadServerURL();
    double nextPoll = 0.0;   // poll the lobby server on a timer

    bool voiceOK = voice_init(g_username);   // mic + speaker + UDP socket (tagged with our name)
    game_init(g_username);                    // 1v1 arena networking

    // First-person player state (arena)
    Vector3 fpPos = { -3, 1.6f, 0 };          // eye position
    float fpYaw = 0, fpPitch = 0;             // look angles (radians)
    int fpHP = 100, fpScore = 0;
    bool inMatchFPS = false;                   // are we an active fighter (vs spectator)
    double nextNetSend = 0, fireCooldown = 0;
    float shotFx = 0;        // >0 = show tracer/muzzle flash, counts down
    bool  shotHit = false;   // last shot connected (for hitmarker)
    Vector3 shotFrom, shotTo;// tracer endpoints

    const char *label = "Folder";
    Rectangle icon = { 80, 80, 90, 70 };
    bool selected = false;
    double lastClick = -1.0;
    const double DOUBLE_CLICK = 0.35;
    bool opened = false;

    Camera3D cam = { 0 };
    cam.position   = (Vector3){ 6.0f, 5.0f, 6.0f };
    cam.target     = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.fovy       = 50.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    float t = 0.0f;
    double quitAt = -1.0;

    // Chat typing state
    bool typing = false;
    char chatInput[200] = "";

    // Login/register screen state
    char loginPw[64] = "";
    int  loginField = 0;          // 0 = username box, 1 = password box
    bool registerMode = false;    // false = log in, true = create account
    char authErr[128] = "";
    bool authBusy = false;
    bool profileOpen = false;     // profile dropdown open on the desktop
    bool settingsOpen = false;    // settings panel open in the lobby
    bool micMuted = false;        // mute push-to-talk entirely
    bool bg3d = false;            // animated 3D background on the desktop
    char selectedProfile[32] = ""; // name of the cube the user clicked (profile card)
    int  masterVol = 100;          // master output volume 0..200
    int  fpsMode = 1;              // 0=60, 1=144, 2=unlimited
    const int fpsValues[3] = { 60, 144, 0 };
    const char *fpsLabels[3] = { "60", "144", "Unlimited" };

    while (!WindowShouldClose())
    {
        if (g_quitForUpdate && quitAt < 0) quitAt = GetTime() + 1.5;
        if (quitAt > 0 && GetTime() >= quitAt) break;

        t += GetFrameTime();   // global animation clock (desktop + lobby)
        SetTargetFPS(fpsValues[fpsMode]);   // 0 = unlimited

        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        // Voice: hold V to talk; always drain incoming audio.
        if (voiceOK) { voice_set_master(masterVol); voice_set_talking(!micMuted && IsKeyDown(KEY_V)); voice_poll(); }
        // Only poll the game network while actually in the arena, and only a few
        // times per second — NOT every frame (blocking curl every frame = lag).
        // Networking runs on a background thread now; nothing to do per-frame.

        // Heartbeat + refresh lists. Slower while in-game so the curl spawns
        // don't hitch the framerate (the ~4s stutter you saw).
        if (g_username[0] && GetTime() >= nextPoll && screen != SCREEN_GAME) {
            PollLobby();
            ReadOnlineFile();
            nextPoll = GetTime() + 2.0;
        }

        int W = GetScreenWidth(), H = GetScreenHeight();
        Vector2 m = GetMousePosition();

        if (screen == SCREEN_USERNAME)
        {
            // Tab / click switches which field has focus.
            if (IsKeyPressed(KEY_TAB)) loginField ^= 1;

            // Type into the focused field.
            char *target = (loginField == 0) ? g_username : loginPw;
            size_t cap   = (loginField == 0) ? sizeof(g_username) : sizeof(loginPw);
            int c = GetCharPressed();
            while (c > 0) {
                size_t len = strlen(target);
                if (c >= 32 && c < 127 && len < cap - 1) { target[len] = (char)c; target[len+1] = '\0'; }
                c = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE)) {
                size_t len = strlen(target);
                if (len > 0) target[len-1] = '\0';
            }

            Rectangle uBox = { W*0.5f - 150, H*0.5f - 58, 300, 38 };
            Rectangle pBox = { W*0.5f - 150, H*0.5f - 8,  300, 38 };
            Rectangle go   = { W*0.5f - 150, H*0.5f + 46, 300, 40 };
            Rectangle swap = { W*0.5f - 150, H*0.5f + 96, 300, 26 };

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (CheckCollisionPointRec(m, uBox)) loginField = 0;
                else if (CheckCollisionPointRec(m, pBox)) loginField = 1;
                else if (CheckCollisionPointRec(m, swap)) { registerMode = !registerMode; authErr[0] = '\0'; }
            }

            bool canGo = strlen(g_username) > 0 && strlen(loginPw) > 0 && !authBusy;
            bool submit = false;
            if (IsKeyPressed(KEY_ENTER) && canGo) submit = true;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, go) && canGo) submit = true;

            if (submit) {
                authBusy = true; authErr[0] = '\0';
                int ok = AuthRequest(registerMode ? "register" : "login",
                                     g_username, loginPw, authErr, sizeof(authErr));
                authBusy = false;
                if (ok) {
                    SaveUsername();        // remember the name locally for convenience
                    loginPw[0] = '\0';     // never keep the password in memory
                    screen = SCREEN_DESKTOP;
                }
            }

            BeginDrawing();
                ClearBackground((Color){16,17,21,255});
                const char *title = registerMode ? "Create account" : "Sign in";
                int ts = 24;
                Txt(title, W/2 - TxtW(title, ts)/2, (int)(H*0.5f - 110), ts, (Color){210,213,222,255});

                // username box
                DrawRectangleRounded(uBox, 0.18f, 6, (Color){26,27,33,255});
                DrawRectangleRoundedLines(uBox, 0.18f, 6,
                    loginField==0 ? (Color){110,115,130,255} : (Color){56,58,68,255});
                char ub[40]; snprintf(ub, sizeof(ub), "%s%s", g_username,
                    (loginField==0 && ((int)(GetTime()*2))%2) ? "_" : "");
                Txt(g_username[0]?ub:"username", (int)uBox.x+14, (int)uBox.y+9, 19,
                    g_username[0]?(Color){220,222,230,255}:(Color){95,98,110,255});

                // password box (masked)
                DrawRectangleRounded(pBox, 0.18f, 6, (Color){26,27,33,255});
                DrawRectangleRoundedLines(pBox, 0.18f, 6,
                    loginField==1 ? (Color){110,115,130,255} : (Color){56,58,68,255});
                char stars[64]; size_t pl = strlen(loginPw);
                for (size_t i=0;i<pl && i<sizeof(stars)-2;i++) stars[i]='*';
                stars[pl<sizeof(stars)-2?pl:sizeof(stars)-2]='\0';
                if (loginField==1 && ((int)(GetTime()*2))%2) strncat(stars,"_",2);
                Txt(loginPw[0]?stars:"password", (int)pBox.x+14, (int)pBox.y+9, 19,
                    loginPw[0]?(Color){220,222,230,255}:(Color){95,98,110,255});

                // submit button (flat, muted)
                Color gbg = !canGo ? (Color){34,35,42,255} : (Color){48,72,110,255};
                DrawRectangleRounded(go, 0.18f, 6, gbg);
                DrawRectangleRoundedLines(go, 0.18f, 6, (Color){70,90,125,160});
                const char *gt = authBusy ? "Please wait..." : (registerMode ? "Create account" : "Sign in");
                Txt(gt, (int)(go.x + go.width/2 - TxtW(gt,17)/2), (int)(go.y+11), 17,
                    canGo ? (Color){225,228,236,255} : (Color){120,123,133,255});

                // toggle login/register
                const char *sw = registerMode ? "Have an account? Sign in"
                                              : "New here? Create an account";
                Txt(sw, (int)(swap.x + swap.width/2 - TxtW(sw,14)/2), (int)swap.y+5, 14, (Color){120,140,180,255});

                // error message
                if (authErr[0])
                    Txt(authErr, W/2 - TxtW(authErr,14)/2, (int)(H*0.5f + 130), 14, (Color){190,120,120,255});

                Txt("Tab to switch fields", W/2 - TxtW("Tab to switch fields",13)/2, (int)(H*0.5f - 142), 13, (Color){95,98,110,255});
            EndDrawing();
        }
        else if (screen == SCREEN_DESKTOP)
        {
            Rectangle btn   = { (float)(W - 150), (float)(H - 52), 130, 34 };
            Rectangle lobby = { (float)(W - 150), (float)(H - 98), 130, 38 };
            // Profile avatar (circle) top-right is now the single menu for
            // Settings / Switch profile / Log out.
            Vector2 avatarC = { (float)(W - 40), 40 };
            float   avatarR = 20;
            Rectangle ddSettings = { W - 190, 66,  170, 34 };
            Rectangle ddSwitch   = { W - 190, 102, 170, 34 };
            Rectangle ddLogout   = { W - 190, 138, 170, 34 };

            bool hoverFolder = CheckCollisionPointRec(m,
                (Rectangle){ icon.x-10, icon.y-10, icon.width+20, icon.height+40 });
            bool overAvatar  = CheckCollisionPointCircle(m, avatarC, avatarR);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                if (overAvatar) { profileOpen = !profileOpen; }
                else if (profileOpen && CheckCollisionPointRec(m, ddSettings)) {
                    settingsOpen = !settingsOpen; profileOpen = false;
                }
                else if (profileOpen && CheckCollisionPointRec(m, ddSwitch)) {
                    g_token[0]='\0'; loginPw[0]='\0'; authErr[0]='\0'; loginField=0;
                    profileOpen = false; screen = SCREEN_USERNAME;
                }
                else if (profileOpen && CheckCollisionPointRec(m, ddLogout)) {
                    g_token[0]='\0'; loginPw[0]='\0'; authErr[0]='\0'; loginField=0;
                    profileOpen = false; screen = SCREEN_USERNAME;
                }
                else if (hoverFolder) {
                    selected = true; profileOpen = false;
                    double now = GetTime();
                    if (now - lastClick <= DOUBLE_CLICK) opened = !opened;
                    lastClick = now;
                } else { selected = false; profileOpen = false; }
            }

            BeginDrawing();
                ClearBackground(BLACK);   // pure black behind the 3D scene

                // --- Optional animated 3D background ---
                if (bg3d) {
                    Camera3D bgCam = { 0 };
                    bgCam.position   = (Vector3){ 0.0f, 0.0f, 8.0f };
                    bgCam.target     = (Vector3){ 0.0f, 0.0f, 0.0f };
                    bgCam.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
                    bgCam.fovy       = 45.0f;
                    bgCam.projection = CAMERA_PERSPECTIVE;

                    BeginMode3D(bgCam);
                        // a slowly tumbling wireframe object
                        rlPushMatrix();
                            rlRotatef(t * 18.0f, 1.0f, 0.0f, 0.0f);
                            rlRotatef(t * 26.0f, 0.0f, 1.0f, 0.0f);
                            DrawCubeWires((Vector3){0,0,0}, 3.0f, 3.0f, 3.0f, (Color){60,110,200,180});
                            DrawSphereWires((Vector3){0,0,0}, 2.0f, 10, 10, (Color){40,70,140,120});
                        rlPopMatrix();
                        // a few orbiting points of light
                        for (int i = 0; i < 6; i++) {
                            float a = t * 0.6f + i * 1.047f;
                            Vector3 p = { cosf(a)*4.5f, sinf(a*1.3f)*2.0f, sinf(a)*4.5f };
                            DrawSphere(p, 0.08f, (Color){120,160,230,220});
                        }
                    EndMode3D();
                }

                Rectangle hit = { icon.x-10, icon.y-10, icon.width+20, icon.height+40 };
                if (selected)      DrawRectangleRounded(hit, 0.15f, 6, (Color){60,90,140,120});
                else if (hoverFolder) DrawRectangleRounded(hit, 0.15f, 6, (Color){40,40,40,120});

                DrawFolder(icon, (Color){235,200,90,255}, (Color){220,185,75,255});
                int fs = 20, tw = TxtW(label, fs);
                Txt(label, (int)(icon.x+icon.width/2 - tw/2), (int)(icon.y+icon.height+6), fs, RAYWHITE);

                if (opened) {
                    Rectangle win = { W*0.5f-300, H*0.5f-200, 600, 400 };
                    DrawRectangleRounded(win, 0.04f, 8, (Color){25,25,28,255});
                    DrawRectangleRoundedLines(win, 0.04f, 8, (Color){80,80,90,255});
                    Txt(label, (int)win.x+16, (int)win.y+12, 20, RAYWHITE);
                    Txt("(empty)", (int)win.x+16, (int)win.y+50, 16, GRAY);
                }

                // Lobby + Update buttons (muted, flat)
                if (Button(lobby, "Enter Lobby", 17, (Color){34,36,44,255}, (Color){46,49,60,255}, (Color){225,227,233,255}, m))
                    screen = SCREEN_LOBBY;
                if (Button(btn, "Check for Update", 15, (Color){26,27,33,255}, (Color){38,40,48,255}, (Color){180,183,193,255}, m)) {
                    strcpy(g_updateMsg, "Checking for updates..."); CheckForUpdate();
                }
                Txt(APP_VERSION, (int)btn.x, (int)(btn.y-22), 14, (Color){110,112,122,255});
                if (g_updateMsg[0]) {
                    int mw = TxtW(g_updateMsg, 14);
                    Txt(g_updateMsg, W-20-mw, H-150, 14, (Color){150,153,163,255});
                }

                // --- Profile avatar (the single menu) ---
                Color av = overAvatar ? (Color){70,74,88,255} : (Color){52,55,66,255};
                DrawCircleV(avatarC, avatarR, av);
                DrawCircleLines((int)avatarC.x, (int)avatarC.y, avatarR, (Color){95,100,115,255});
                char initial[2] = { (char)(g_username[0] ? toupper((unsigned char)g_username[0]) : '?'), '\0' };
                Txt(initial, (int)(avatarC.x - TxtW(initial,18)/2), (int)(avatarC.y - 10), 18, (Color){210,213,222,255});

                // --- Avatar dropdown: Settings / Switch profile / Log out ---
                if (profileOpen) {
                    Rectangle dd = { W - 200, 64, 190, 116 };
                    DrawRectangleRounded(dd, 0.08f, 8, (Color){24,25,31,255});
                    DrawRectangleRoundedLines(dd, 0.08f, 8, (Color){70,72,84,255});
                    if (CheckCollisionPointRec(m, ddSettings)) DrawRectangleRounded(ddSettings, 0.2f, 6, (Color){40,42,52,255});
                    if (CheckCollisionPointRec(m, ddSwitch))   DrawRectangleRounded(ddSwitch,   0.2f, 6, (Color){40,42,52,255});
                    if (CheckCollisionPointRec(m, ddLogout))   DrawRectangleRounded(ddLogout,   0.2f, 6, (Color){40,42,52,255});
                    Txt("Settings",       (int)ddSettings.x+12, (int)ddSettings.y+9, 15, (Color){210,213,222,255});
                    Txt("Switch profile", (int)ddSwitch.x+12,   (int)ddSwitch.y+9,   15, (Color){210,213,222,255});
                    Txt("Log out",        (int)ddLogout.x+12,   (int)ddLogout.y+9,   15, (Color){190,150,150,255});
                }

                // --- Settings panel (opened from the avatar menu) ---
                if (settingsOpen) {
                    Rectangle sp = { W*0.5f - 150, H*0.5f - 170, 300, 344 };
                    DrawRectangle(0,0,W,H,(Color){0,0,0,110});
                    DrawRectangleRounded(sp, 0.05f, 8, (Color){22,23,29,255});
                    DrawRectangleRoundedLines(sp, 0.05f, 8, (Color){68,70,82,255});
                    Txt("Settings", (int)sp.x+18, (int)sp.y+14, 18, (Color){205,208,218,255});

                    Txt(TextFormat("Output volume: %d%%", masterVol), (int)sp.x+18, (int)sp.y+48, 14, (Color){150,153,163,255});
                    masterVol = Slider((Rectangle){sp.x+18, sp.y+68, sp.width-36, 20}, masterVol, 200, m);

                    Rectangle muteBtn = { sp.x+18, sp.y+100, sp.width-36, 34 };
                    if (Button(muteBtn, micMuted ? "Microphone: muted" : "Microphone: on", 14,
                               (Color){30,32,40,255}, (Color){42,45,55,255},
                               micMuted ? (Color){195,150,150,255} : (Color){170,200,180,255}, m))
                        micMuted = !micMuted;

                    Rectangle bgB = { sp.x+18, sp.y+140, sp.width-36, 34 };
                    if (Button(bgB, bg3d ? "3D background: on" : "3D background: off", 14,
                               (Color){30,32,40,255}, (Color){42,45,55,255},
                               bg3d ? (Color){170,185,215,255} : (Color){150,153,163,255}, m))
                        bg3d = !bg3d;

                    Rectangle fpsB = { sp.x+18, sp.y+180, sp.width-36, 34 };
                    if (Button(fpsB, TextFormat("FPS limit: %s", fpsLabels[fpsMode]), 14,
                               (Color){30,32,40,255}, (Color){42,45,55,255}, (Color){170,185,215,255}, m))
                        fpsMode = (fpsMode + 1) % 3;

                    Rectangle resB = { sp.x+18, sp.y+220, sp.width-36, 34 };
                    if (Button(resB, TextFormat("Resolution: %s", resLabel[resMode]), 14,
                               (Color){30,32,40,255}, (Color){42,45,55,255}, (Color){170,185,215,255}, m)) {
                        resMode = (resMode + 1) % 5;
                        // If fullscreen, drop to windowed so the new size is visible.
                        if (IsWindowFullscreen()) ToggleFullscreen();
                        int mm = GetCurrentMonitor();
                        int rw = resW[resMode], rh = resH[resMode];
                        // never exceed the monitor
                        if (rw > GetMonitorWidth(mm))  rw = GetMonitorWidth(mm);
                        if (rh > GetMonitorHeight(mm)) rh = GetMonitorHeight(mm);
                        SetWindowSize(rw, rh);
                        SetWindowPosition((GetMonitorWidth(mm)-rw)/2, (GetMonitorHeight(mm)-rh)/2);
                    }

                    Rectangle fsB = { sp.x+18, sp.y+260, sp.width-36, 34 };
                    if (Button(fsB, "Fullscreen", 14, (Color){30,32,40,255}, (Color){42,45,55,255}, (Color){180,183,193,255}, m))
                        ToggleFullscreen();

                    Rectangle clB = { sp.x+sp.width-38, sp.y+10, 28, 28 };
                    if (Button(clB, "X", 15, (Color){34,35,42,255}, (Color){46,48,58,255}, (Color){190,193,203,255}, m))
                        settingsOpen = false;
                }
            EndDrawing();
        }
        else if (screen == SCREEN_LOBBY)
        {

            // ---- Chat typing ----
            if (typing) {
                int ch = GetCharPressed();
                while (ch > 0) {
                    size_t len = strlen(chatInput);
                    if (ch >= 32 && ch < 127 && len < sizeof(chatInput) - 1) {
                        chatInput[len] = (char)ch; chatInput[len+1] = '\0';
                    }
                    ch = GetCharPressed();
                }
                if (IsKeyPressed(KEY_BACKSPACE)) {
                    size_t len = strlen(chatInput);
                    if (len > 0) chatInput[len-1] = '\0';
                }
                if (IsKeyPressed(KEY_ENTER)) {
                    SendChat(chatInput);
                    chatInput[0] = '\0';
                    typing = false;
                }
                if (IsKeyPressed(KEY_ESCAPE)) { chatInput[0] = '\0'; typing = false; }
            } else {
                if (IsKeyPressed(KEY_T)) typing = true;       // T opens chat
                if (IsKeyPressed(KEY_ESCAPE)) screen = SCREEN_DESKTOP;
            }

            Rectangle back = { 20, 20, 90, 36 };
            bool hoverBack = CheckCollisionPointRec(m, back);
            if (hoverBack && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) screen = SCREEN_DESKTOP;

            Vector3 cubePos = { 0.0f, 1.5f + sinf(t * 1.5f) * 0.5f, 0.0f };

            BeginDrawing();
                ClearBackground(RAYWHITE);

                // Parse online names into an array.
                char namebuf[2048];
                strncpy(namebuf, g_online, sizeof(namebuf)-1); namebuf[sizeof(namebuf)-1]='\0';
                char *names[32]; int people = 0;
                {
                    char *ln = strtok(namebuf, "\n");
                    while (ln && people < 32) { if (ln[0]) names[people++] = ln; ln = strtok(NULL, "\n"); }
                }

                // Compute each person-cube's world position (slow rotation).
                Vector3 cubeWorld[32];
                float radius = 3.5f;
                for (int i = 0; i < people; i++) {
                    float a = (6.2831853f * i / (people > 0 ? people : 1)) + t * 0.12f; // slower spin
                    cubeWorld[i] = (Vector3){ cosf(a)*radius, 0.6f + sinf(t*0.8f + i)*0.12f, sinf(a)*radius };
                }

                BeginMode3D(cam);
                    DrawPlane((Vector3){0,0,0}, (Vector2){20,20}, (Color){235,235,235,255});
                    DrawCube(cubePos, 1.5f, 1.5f, 1.5f, RED);
                    DrawCubeWires(cubePos, 1.5f, 1.5f, 1.5f, MAROON);
                    for (int i = 0; i < people; i++) {
                        bool isMe = (strcmp(names[i], g_username) == 0);
                        Color cc = isMe ? (Color){80,170,110,255} : (Color){60,90,200,255};
                        DrawCube(cubeWorld[i], 0.7f, 0.7f, 0.7f, cc);
                        DrawCubeWires(cubeWorld[i], 0.7f, 0.7f, 0.7f, (Color){30,50,140,255});
                    }
                EndMode3D();

                // Project cubes to screen; draw name tags + handle clicks.
                for (int i = 0; i < people; i++) {
                    Vector2 sp = GetWorldToScreen(cubeWorld[i], cam);
                    int tw = TxtW(names[i], 16);
                    Txt(names[i], (int)(sp.x - tw/2), (int)(sp.y - 50), 16, (Color){40,40,50,255});
                    // clickable region around the cube on screen
                    Rectangle hitR = { sp.x - 35, sp.y - 35, 70, 70 };
                    if (CheckCollisionPointRec(m, hitR) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                        strncpy(selectedProfile, names[i], sizeof(selectedProfile)-1);
                }

                Txt("LOBBY", 20, H-50, 28, (Color){40,40,40,255});

                // Push-to-talk hint / indicator
                if (!voiceOK) {
                    Txt("voice off (mic/socket failed)", 20, H-78, 16, (Color){170,90,90,255});
                } else if (micMuted) {
                    Txt("Mic muted (Settings to unmute)", 20, H-100, 18, (Color){170,90,90,255});
                } else if (voice_is_talking()) {
                    DrawCircle(40, H-92, 10, (Color){220,40,40,255});
                    Txt("TALKING (hold V)", 60, H-100, 18, (Color){200,40,40,255});
                } else {
                    Txt("Hold V to talk", 20, H-100, 18, (Color){90,90,90,255});
                }

                // ---- Online panel (top-right): live list from the server ----
                {
                    int px = W - 260, py = 70, pw = 240;
                    // count names for panel height
                    int count = 0;
                    for (const char *p = g_online; *p; p++) if (*p == '\n') count++;
                    int ph = 50 + (count > 0 ? count : 1) * 22;
                    DrawRectangleRounded((Rectangle){px,py,pw,ph}, 0.08f, 6, (Color){245,245,245,235});
                    DrawRectangleRoundedLines((Rectangle){px,py,pw,ph}, 0.08f, 6, (Color){180,180,180,255});
                    Txt("Online", px+14, py+10, 20, (Color){40,40,40,255});

                    // draw each line
                    char buf[2048];
                    strncpy(buf, g_online, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
                    int y = py + 40;
                    int shown = 0;
                    char *line = strtok(buf, "\n");
                    while (line) {
                        if (line[0]) {
                            bool me = (strcmp(line, g_username) == 0);
                            Txt(me ? TextFormat("%s (you)", line) : line,
                                     px+18, y, 16, me ? (Color){40,110,60,255} : (Color){60,60,60,255});
                            y += 22; shown++;
                        }
                        line = strtok(NULL, "\n");
                    }
                    if (shown == 0)
                        Txt("(connecting...)", px+18, y, 14, GRAY);

                    // ---- Friends panel, just below Online ----
                    int fpy = py + ph + 12;
                    int fcount = 0;
                    for (const char *p = g_friends; *p; p++) if (*p=='\n') fcount++;
                    int fph = 44 + (fcount>0?fcount:1)*22;
                    DrawRectangleRounded((Rectangle){px,fpy,pw,fph}, 0.08f, 6, (Color){245,245,245,235});
                    DrawRectangleRoundedLines((Rectangle){px,fpy,pw,fph}, 0.08f, 6, (Color){180,180,180,255});
                    Txt("Friends", px+14, fpy+10, 18, (Color){40,40,40,255});
                    char fbuf[2048]; strncpy(fbuf, g_friends, sizeof(fbuf)-1); fbuf[sizeof(fbuf)-1]='\0';
                    int fy = fpy + 38, fsh = 0;
                    char *fl = strtok(fbuf, "\n");
                    while (fl) {
                        if (fl[0]) {
                            // green if that friend is currently online
                            int online = (strstr(g_online, fl) != NULL);
                            Txt(fl, px+18, fy, 15, online ? (Color){40,140,70,255} : (Color){130,130,140,255});
                            if (online) Txt("online", px+pw-58, fy, 12, (Color){40,140,70,255});
                            fy += 22; fsh++;
                        }
                        fl = strtok(NULL, "\n");
                    }
                    if (fsh == 0) Txt("(none yet)", px+18, fy, 13, GRAY);

                    // ---- Leaderboard, below Friends ----
                    int lpy = fpy + fph + 12;
                    int lc = 0; for (const char *p=g_leaderboard; *p; p++) if (*p=='\n') lc++;
                    int lph = 44 + (lc>0?lc:1)*20;
                    DrawRectangleRounded((Rectangle){px,lpy,pw,lph}, 0.08f, 6, (Color){245,245,245,235});
                    DrawRectangleRoundedLines((Rectangle){px,lpy,pw,lph}, 0.08f, 6, (Color){180,180,180,255});
                    Txt("Leaderboard (W-L)", px+14, lpy+10, 16, (Color){40,40,40,255});
                    char lbuf[1024]; strncpy(lbuf,g_leaderboard,sizeof(lbuf)-1); lbuf[sizeof(lbuf)-1]='\0';
                    int ly = lpy+36, lsh=0, rank=1;
                    char *lln = strtok(lbuf,"\n");
                    while (lln) {
                        if (lln[0]) {
                            Txt(TextFormat("%d. %s", rank, lln), px+18, ly, 14, (Color){60,60,70,255});
                            ly += 20; lsh++; rank++;
                        }
                        lln = strtok(NULL,"\n");
                    }
                    if (lsh==0) Txt("(no games yet)", px+18, ly, 13, GRAY);
                }
                Txt(TextFormat("server: %s", g_serverURL), 20, H-20, 14, GRAY);

                // ---- Floating chat bubble (gently bobs) ----
                {
                    float bx = 40 + sinf(t * 0.8f) * 12.0f;       // drift sideways
                    float by = 120 + sinf(t * 1.1f) * 10.0f;      // bob vertically
                    int bw = 320, bh = 220;
                    DrawRectangleRounded((Rectangle){bx,by,bw,bh}, 0.12f, 8, (Color){255,255,255,235});
                    DrawRectangleRoundedLines((Rectangle){bx,by,bw,bh}, 0.12f, 8, (Color){200,200,210,255});
                    // little tail to make it a speech bubble
                    DrawTriangle((Vector2){bx+30,by+bh}, (Vector2){bx+60,by+bh},
                                 (Vector2){bx+25,by+bh+22}, (Color){255,255,255,235});
                    Txt("Chat", (int)bx+14, (int)by+8, 18, (Color){60,60,70,255});

                    // last few chat lines
                    char cbuf[4096];
                    strncpy(cbuf, g_chat, sizeof(cbuf)-1); cbuf[sizeof(cbuf)-1]='\0';
                    // collect lines
                    const int MAXL = 8;
                    char *lines[64]; int nl = 0;
                    char *ln = strtok(cbuf, "\n");
                    while (ln) { if (ln[0]) { lines[nl % 64] = ln; nl++; } ln = strtok(NULL, "\n"); }
                    int start = nl > MAXL ? nl - MAXL : 0;
                    int yy = (int)by + 36;
                    for (int i = start; i < nl; i++) {
                        Txt(lines[i % 64], (int)bx+14, yy, 14, (Color){50,50,60,255});
                        yy += 20;
                    }
                    if (nl == 0) Txt("(no messages yet)", (int)bx+14, yy, 14, GRAY);

                    // input row
                    if (typing) {
                        DrawRectangle((int)bx+10, (int)by+bh-30, bw-20, 24, (Color){235,238,245,255});
                        Txt(TextFormat("%s_", chatInput), (int)bx+14, (int)by+bh-26, 14, (Color){20,20,30,255});
                    } else {
                        Txt("Press T to chat", (int)bx+14, (int)by+bh-26, 14, (Color){130,130,140,255});
                    }
                }

                Color bbg = hoverBack ? (Color){200,200,200,255} : (Color){220,220,220,255};
                DrawRectangleRounded(back, 0.3f, 6, bbg);
                DrawRectangleRoundedLines(back, 0.3f, 6, (Color){150,150,150,255});
                Txt("< Back", 34, 28, 18, (Color){40,40,40,255});

                // ---- Play 1v1 button (top-center) ----
                {
                    Rectangle pvp = { W*0.5f - 90, 24, 180, 40 };
                    if (Button(pvp, "Play 1v1", 18, (Color){40,42,52,255}, (Color){54,57,70,255}, (Color){225,227,233,255}, m)) {
                        GameJoin();
                        screen = SCREEN_GAME;
                    }
                }

                // ---- Profile card (when a cube is clicked) ----
                if (selectedProfile[0]) {
                    Rectangle pc = { W*0.5f - 170, H*0.5f - 175, 340, 350 };
                    DrawRectangle(0, 0, W, H, (Color){0,0,0,110});   // dim behind
                    DrawRectangleRounded(pc, 0.05f, 8, (Color){26,27,33,255});
                    DrawRectangleRoundedLines(pc, 0.05f, 8, (Color){70,72,84,255});

                    bool isSelf = (strcmp(selectedProfile, g_username) == 0);

                    // avatar circle with initial
                    Vector2 ac = { pc.x + pc.width/2, pc.y + 60 };
                    DrawCircleV(ac, 34, (Color){52,55,66,255});
                    DrawCircleLines((int)ac.x, (int)ac.y, 34, (Color){95,100,115,255});
                    char ini[2] = { (char)toupper((unsigned char)selectedProfile[0]), '\0' };
                    Txt(ini, (int)(ac.x - TxtW(ini,30)/2), (int)(ac.y - 16), 30, (Color){215,218,226,255});

                    int nw = TxtW(selectedProfile, 22);
                    Txt(selectedProfile, (int)(pc.x + pc.width/2 - nw/2), (int)(pc.y+104), 22, (Color){220,222,230,255});
                    const char *status = "Online";
                    Txt(status, (int)(pc.x + pc.width/2 - TxtW(status,13)/2), (int)(pc.y+134), 13, (Color){120,170,135,255});

                    if (isSelf) {
                        Txt("(this is you)", (int)(pc.x+pc.width/2 - TxtW("(this is you)",14)/2),
                            (int)(pc.y+170), 14, (Color){110,113,125,255});
                    } else {
                        // Per-person volume
                        int pv = voice_get_peer_volume(selectedProfile);
                        Txt(TextFormat("Volume: %d%%", pv), (int)pc.x+24, (int)pc.y+168, 14, (Color){150,153,163,255});
                        Rectangle pvSld = { pc.x+24, pc.y+188, pc.width-48, 20 };
                        int newpv = Slider(pvSld, pv, 200, m);
                        if (newpv != pv) voice_set_peer_volume(selectedProfile, newpv);

                        // Per-person mute
                        int pm = voice_get_peer_mute(selectedProfile);
                        Rectangle mB = { pc.x+24, pc.y+220, pc.width-48, 34 };
                        if (Button(mB, pm ? "Unmute this person" : "Mute this person", 14,
                                   (Color){32,34,42,255}, (Color){44,47,57,255},
                                   pm ? (Color){195,150,150,255} : (Color){200,203,213,255}, m))
                            voice_set_peer_mute(selectedProfile, !pm);

                        // Add Friend / already-friend indicator
                        Rectangle addB = { pc.x+24, pc.y+262, pc.width-48, 34 };
                        if (IsFriend(selectedProfile)) {
                            DrawRectangleRounded(addB, 0.16f, 6, (Color){30,40,32,255});
                            Txt("Friends", (int)(addB.x+addB.width/2 - TxtW("Friends",14)/2), (int)(addB.y+10), 14, (Color){140,190,150,255});
                        } else if (Button(addB, "Add Friend", 14, (Color){44,62,92,255}, (Color){56,78,114,255}, (Color){220,225,235,255}, m)) {
                            AddFriend(selectedProfile);
                        }
                    }

                    // Close button
                    Rectangle clB = { pc.x+pc.width-40, pc.y+10, 28, 28 };
                    if (Button(clB, "X", 15, (Color){34,35,42,255}, (Color){46,48,58,255}, (Color){190,193,203,255}, m))
                        selectedProfile[0] = '\0';
                }

                // ---- Settings button (bottom-right) ----
                Rectangle setBtn = { (float)(W - 150), (float)(H - 50), 130, 32 };
                if (Button(setBtn, "Settings", 15, (Color){235,235,238,255}, (Color){222,222,228,255}, (Color){50,52,62,255}, m))
                    settingsOpen = !settingsOpen;

                // ---- Settings panel (audio controls) ----
                if (settingsOpen) {
                    Rectangle sp = { (float)(W - 300), (float)(H - 256), 280, 196 };
                    DrawRectangleRounded(sp, 0.05f, 8, (Color){246,247,249,255});
                    DrawRectangleRoundedLines(sp, 0.05f, 8, (Color){205,207,215,255});
                    Txt("Audio", (int)sp.x+16, (int)sp.y+13, 17, (Color){50,52,62,255});

                    // Master output volume slider
                    Txt(TextFormat("Output volume: %d%%", masterVol), (int)sp.x+16, (int)sp.y+44, 14, (Color){90,92,104,255});
                    Rectangle volSld = { sp.x+16, sp.y+64, sp.width-32, 20 };
                    masterVol = Slider(volSld, masterVol, 200, m);

                    // Mic toggle
                    Rectangle muteBtn = { sp.x+16, sp.y+96, sp.width-32, 34 };
                    if (Button(muteBtn, micMuted ? "Microphone: muted" : "Microphone: on", 14,
                               (Color){236,238,242,255}, (Color){228,230,236,255},
                               micMuted ? (Color){180,90,90,255} : (Color){70,120,80,255}, m))
                        micMuted = !micMuted;

                    // Fullscreen
                    Rectangle fsB = { sp.x+16, sp.y+138, sp.width-32, 34 };
                    if (Button(fsB, "Fullscreen", 14, (Color){236,238,242,255}, (Color){228,230,236,255}, (Color){70,72,84,255}, m))
                        ToggleFullscreen();
                }
            EndDrawing();
        }
        else // SCREEN_GAME — first-person 1v1 arena
        {
            float dt = GetFrameTime();
            int spectator = (g_myRole == 2);

            // Match info from the lobby server (scores/round/winner).
            char p0[32], p1[32], win[32];
            GsStr("p0", p0, sizeof(p0)); GsStr("p1", p1, sizeof(p1)); GsStr("winner", win, sizeof(win));
            int s0 = GsInt("s0"), s1 = GsInt("s1"), round = GsInt("round");
            int filled = (p0[0]?1:0) + (p1[0]?1:0);

            // ---- Arena geometry: bigger floor 24x24, four cover boxes ----
            #define NCOVER 4
            Vector3 coverPos[NCOVER]  = { {-5,1,-5}, {5,1,5}, {-5,1,5}, {5,1,-5} };
            Vector3 coverSize = { 2.6f, 2.0f, 2.6f };
            Vector3 treePos = { 0, 0, 0 };        // tree trunk base at center
            float   treeR = 0.9f;                 // collision radius around trunk

            // Spawn point per slot (further apart in the bigger room).
            if (!inMatchFPS && !spectator) {
                fpPos = (g_myRole==1) ? (Vector3){8,1.6f,8} : (Vector3){-8,1.6f,-8};
                fpYaw = (g_myRole==1) ? 3.14159f : 0.0f;
                fpPitch = 0; fpHP = 100; inMatchFPS = true;
            }

            // ---- Mouse look (only when not in menus) ----
            if (!spectator && !win[0]) {
                Vector2 md = GetMouseDelta();
                fpYaw   += md.x * 0.003f;
                fpPitch -= md.y * 0.003f;
                if (fpPitch >  1.5f) fpPitch =  1.5f;
                if (fpPitch < -1.5f) fpPitch = -1.5f;
                DisableCursor();
            } else {
                EnableCursor();
            }

            // ---- WASD movement with simple cover collision ----
            if (!spectator && !win[0]) {
                float spd = 5.0f * dt;
                Vector3 fwd = { cosf(fpYaw), 0, sinf(fpYaw) };
                Vector3 rgt = { -sinf(fpYaw), 0, cosf(fpYaw) };
                Vector3 np = fpPos;
                if (IsKeyDown(KEY_W)) { np.x += fwd.x*spd; np.z += fwd.z*spd; }
                if (IsKeyDown(KEY_S)) { np.x -= fwd.x*spd; np.z -= fwd.z*spd; }
                if (IsKeyDown(KEY_D)) { np.x += rgt.x*spd; np.z += rgt.z*spd; }
                if (IsKeyDown(KEY_A)) { np.x -= rgt.x*spd; np.z -= rgt.z*spd; }
                // keep inside the bigger walls
                if (np.x >  11.3f) np.x =  11.3f; if (np.x < -11.3f) np.x = -11.3f;
                if (np.z >  11.3f) np.z =  11.3f; if (np.z < -11.3f) np.z = -11.3f;
                // block cover boxes
                int blocked = 0;
                for (int i=0;i<NCOVER;i++) {
                    float hx=coverSize.x/2+0.4f, hz=coverSize.z/2+0.4f;
                    if (np.x > coverPos[i].x-hx && np.x < coverPos[i].x+hx &&
                        np.z > coverPos[i].z-hz && np.z < coverPos[i].z+hz) { blocked=1; break; }
                }
                // block the center tree (circular)
                float tdx = np.x-treePos.x, tdz = np.z-treePos.z;
                if (tdx*tdx + tdz*tdz < (treeR+0.4f)*(treeR+0.4f)) blocked = 1;
                if (!blocked) { fpPos.x = np.x; fpPos.z = np.z; }
            }

            // ---- Build look direction + FPS camera ----
            Vector3 dir = { cosf(fpPitch)*cosf(fpYaw), sinf(fpPitch), cosf(fpPitch)*sinf(fpYaw) };
            Camera3D gcam = { 0 };
            gcam.position = fpPos;
            gcam.target   = (Vector3){ fpPos.x+dir.x, fpPos.y+dir.y, fpPos.z+dir.z };
            gcam.up = (Vector3){0,1,0}; gcam.fovy = 70; gcam.projection = CAMERA_PERSPECTIVE;

            // Opponent state.
            RemotePlayer opp; int haveOpp = game_get_opponent(&opp);

            // ---- Shooting: raycast from camera toward opponent ----
            if (!spectator && !win[0] && fireCooldown <= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                fireCooldown = 0.4f;
                shotFx = 0.12f; shotHit = false;
                // tracer goes from just below the eye, straight out along aim
                shotFrom = (Vector3){ fpPos.x + dir.x*0.3f, fpPos.y - 0.2f, fpPos.z + dir.z*0.3f };
                shotTo   = (Vector3){ fpPos.x + dir.x*40, fpPos.y + dir.y*40, fpPos.z + dir.z*40 };
                if (haveOpp && opp.hp > 0) {
                    Vector3 oc = { opp.x, opp.y, opp.z };
                    Vector3 to = { oc.x-fpPos.x, oc.y-fpPos.y, oc.z-fpPos.z };
                    float len = sqrtf(to.x*to.x+to.y*to.y+to.z*to.z);
                    if (len > 0.001f) {
                        Vector3 tn = { to.x/len, to.y/len, to.z/len };
                        float dot = tn.x*dir.x + tn.y*dir.y + tn.z*dir.z; // 1 = dead on
                        if (dot > 0.985f) { game_send_hit(opp.name, 34); shotHit = true; shotTo = oc; }
                    }
                }
            }
            if (fireCooldown > 0) fireCooldown -= dt;
            if (shotFx > 0) shotFx -= dt;

            // ---- Take incoming damage ----
            int dmg = game_take_damage();
            if (dmg > 0 && !win[0]) {
                fpHP -= dmg;
                if (fpHP <= 0) {
                    fpHP = 0;
                    // I died -> opponent won this round. Report THEIR score.
                    // (each client reports its own round losses by letting the killer score:
                    //  we tell the server the opponent scored by calling /score as them is not
                    //  possible, so instead the WINNER reports when they see opp hp hit 0.)
                }
            }
            // If I see the opponent at 0 hp, I (the winner) report the round.
            static double scoreLock = 0;
            if (haveOpp && opp.hp <= 0 && fpHP > 0 && GetTime() > scoreLock && !win[0]) {
                char cmd[512], out[64];
                snprintf(cmd,sizeof(cmd),"curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"\"%s/score?name=%s\"", g_serverURL, g_username);
                RunCapture(cmd,out,sizeof(out));
                scoreLock = GetTime() + 3.0;     // avoid double-report
                fpHP = 100;                      // reset my health for next round
                inMatchFPS = false;              // respawn next frame
            }
            // If I died, reset for next round after a moment.
            if (fpHP <= 0 && GetTime() > scoreLock) { scoreLock = GetTime()+3.0; inMatchFPS=false; }

            // ---- Send my state ~20x/sec ----
            if (!spectator) {
                // instant now (just updates shared state; worker thread sends it)
                game_send_state(fpPos.x, fpPos.y, fpPos.z, fpYaw, fpPitch, fpHP, fpScore);
                (void)nextNetSend;
            }

            if (IsKeyPressed(KEY_ESCAPE)) { EnableCursor(); inMatchFPS=false; screen = SCREEN_LOBBY; }

            // ---- DRAW ----
            BeginDrawing();
                ClearBackground((Color){235,236,240,255});
                BeginMode3D(gcam);
                    DrawPlane((Vector3){0,0,0},(Vector2){24,24},(Color){248,248,250,255});
                    DrawCubeWires((Vector3){0,3.0f,0},24,6,24,(Color){205,207,214,255});
                    // cover boxes
                    for (int i=0;i<NCOVER;i++){
                        DrawCubeV(coverPos[i], coverSize, (Color){200,202,210,255});
                        DrawCubeWiresV(coverPos[i], coverSize, (Color){150,152,162,255});
                    }
                    // center tree: brown trunk + green foliage
                    DrawCylinder(treePos, 0.5f, 0.5f, 3.0f, 12, (Color){120,82,45,255});
                    DrawCylinderWires(treePos, 0.5f, 0.5f, 3.0f, 12, (Color){90,60,32,255});
                    DrawSphere((Vector3){treePos.x, 3.6f, treePos.z}, 1.8f, (Color){70,150,70,255});
                    DrawSphere((Vector3){treePos.x-1.0f, 3.0f, treePos.z+0.6f}, 1.2f, (Color){80,165,80,255});
                    DrawSphere((Vector3){treePos.x+1.0f, 3.2f, treePos.z-0.5f}, 1.2f, (Color){60,140,60,255});
                    // opponent as a body cube + head
                    if (haveOpp && opp.hp > 0) {
                        Color oc = (Color){200,70,70,255};
                        DrawCube((Vector3){opp.x,opp.y,opp.z}, 0.9f,1.6f,0.9f, oc);
                        DrawCubeWires((Vector3){opp.x,opp.y,opp.z}, 0.9f,1.6f,0.9f, MAROON);
                        DrawSphere((Vector3){opp.x,opp.y+1.0f,opp.z}, 0.35f, oc);
                    }
                    // shot tracer (visible bullet line) + impact spark
                    if (shotFx > 0) {
                        DrawLine3D(shotFrom, shotTo, (Color){255,220,90,255});
                        DrawSphere(shotTo, shotHit?0.25f:0.12f, shotHit?(Color){255,80,60,255}:(Color){255,220,90,255});
                    }
                EndMode3D();

                // crosshair (turns red briefly on a hit)
                if (!spectator && !win[0]) {
                    Color ch = (shotFx>0 && shotHit) ? (Color){220,40,40,255} : (Color){40,40,50,200};
                    DrawLine(W/2-10, H/2, W/2+10, H/2, ch);
                    DrawLine(W/2, H/2-10, W/2, H/2+10, ch);
                    // hitmarker X when you connect
                    if (shotFx>0 && shotHit) {
                        DrawLine(W/2-14,H/2-14,W/2-6,H/2-6,(Color){220,40,40,255});
                        DrawLine(W/2+14,H/2-14,W/2+6,H/2-6,(Color){220,40,40,255});
                        DrawLine(W/2-14,H/2+14,W/2-6,H/2+6,(Color){220,40,40,255});
                        DrawLine(W/2+14,H/2+14,W/2+6,H/2+6,(Color){220,40,40,255});
                    }
                    // muzzle flash bottom-center
                    if (shotFx>0) {
                        float a = shotFx*1500; if (a>255) a=255;
                        DrawCircle(W/2, H-120, 8+shotFx*40, (Color){255,210,80,(unsigned char)a});
                    }
                }

                // health bar
                if (!spectator) {
                    DrawRectangle(24, H-44, 220, 22, (Color){210,210,216,255});
                    DrawRectangle(24, H-44, (int)(220*(fpHP/100.0f)), 22, (Color){70,160,90,255});
                    Txt(TextFormat("HP %d", fpHP), 30, H-42, 16, (Color){30,30,40,255});
                }

                // scoreboard top
                char cnt[16]; snprintf(cnt,sizeof(cnt),"%d/2", filled);
                Txt(cnt, W/2 - TxtW(cnt,26)/2, 12, 26, (Color){40,40,50,255});
                Txt(p0[0]?p0:"(waiting)", 24, 48, 18, (Color){200,60,60,255});
                Txt(TextFormat("%d", s0), 24, 72, 24, (Color){40,40,50,255});
                Txt(p1[0]?p1:"(waiting)", W-24-TxtW(p1[0]?p1:"(waiting)",18), 48, 18, (Color){60,90,200,255});
                Txt(TextFormat("%d", s1), W-44, 72, 24, (Color){40,40,50,255});
                Txt(TextFormat("Round %d  -  Best of 3", round), W/2 - TxtW("Round 1  -  Best of 3",15)/2, 44, 15, (Color){110,110,120,255});

                if (spectator)
                    Txt("Spectating (match full)", W/2 - TxtW("Spectating (match full)",18)/2, H-70, 18, (Color){150,120,60,255});

                if (win[0]) {
                    EnableCursor();
                    const char *wt = TextFormat("%s wins the match!", win);
                    DrawRectangle(0,H/2-50,W,100,(Color){0,0,0,120});
                    Txt(wt, W/2 - TxtW(wt,30)/2, H/2-30, 30, RAYWHITE);
                    Rectangle rb = { W*0.5f-80, H/2+12, 160, 34 };
                    if (Button(rb, "Back to lobby", 16, (Color){40,42,52,255},(Color){54,57,70,255},RAYWHITE,m)) {
                        // reset match on server, return
                        char cmd[256], out[32];
                        snprintf(cmd,sizeof(cmd),"curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\"\"%s/resetmatch\"", g_serverURL);
                        RunCapture(cmd,out,sizeof(out));
                        inMatchFPS=false; screen=SCREEN_LOBBY;
                    }
                }

                Txt("WASD move  -  Mouse look  -  Click shoot  -  Esc leave",
                    24, H-72, 13, (Color){120,120,130,255});
            EndDrawing();
        }
    }

    voice_shutdown();
    game_shutdown();
    if (g_fontOK) UnloadFont(g_font);
    CloseWindow();
    return 0;
}
