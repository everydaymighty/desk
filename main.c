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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Voice (mic/speaker/UDP) lives in voice.c behind this small API, so its
// windows.h include never clashes with raylib.
#include "voice.h"

// ---- Bump this every time you publish a new GitHub release ----
#define APP_VERSION "v0.1.0"
#define GH_OWNER    "everydaymighty"
#define GH_REPO     "desk"

typedef enum { SCREEN_USERNAME, SCREEN_DESKTOP, SCREEN_LOBBY } Screen;

static char g_updateMsg[256] = "";

// ---- Username, saved locally next to the exe (no network) ----
#define USER_FILE "desk_user.txt"
static char g_username[32] = "";

// ---- Lobby networking (HTTP polling) ----
// Server base URL is read from desk_server.txt (e.g. http://localhost:8080
// for local testing, or your ngrok https URL for other computers). If the file
// is missing we default to localhost.
#define SERVER_FILE "desk_server.txt"
#define ONLINE_FILE "desk_online.txt"
#define CHAT_FILE   "desk_chat.txt"
static char g_serverURL[256] = "http://localhost:8080";
static char g_online[2048]   = "";   // raw newline-separated names from server
static char g_chat[4096]     = "";   // raw newline-separated chat lines from server

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
        "cmd /c start \"\" /b curl -s -m 4 \"%s/hello?name=%s\" >NUL 2>&1",
        g_serverURL, g_username);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -o \"%s\" \"%s/online\" 2>NUL",
        ONLINE_FILE, g_serverURL);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -o \"%s\" \"%s/chat\" 2>NUL",
        CHAT_FILE, g_serverURL);
    system(cmd);
#else
    snprintf(cmd, sizeof(cmd),
        "( curl -s -m 4 \"%s/hello?name=%s\" >/dev/null 2>&1; "
        "  curl -s -m 4 -o \"%s\" \"%s/online\" 2>/dev/null; "
        "  curl -s -m 4 -o \"%s\" \"%s/chat\" 2>/dev/null ) &",
        g_serverURL, g_username, ONLINE_FILE, g_serverURL, CHAT_FILE, g_serverURL);
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
        "cmd /c start \"\" /b curl -s -m 4 \"%s/say?name=%s&msg=%s\" >NUL 2>&1",
        g_serverURL, g_username, enc);
#else
    snprintf(cmd, sizeof(cmd),
        "curl -s -m 4 \"%s/say?name=%s&msg=%s\" >/dev/null 2>&1 &",
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
                 "Could not check (no internet or no releases yet).");
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
    // Start in a normal resizable window. Press F11 to toggle fullscreen.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Desktop");
    SetTargetFPS(60);

    Screen screen = LoadUsername() ? SCREEN_DESKTOP : SCREEN_USERNAME;
    LoadServerURL();
    double nextPoll = 0.0;   // poll the lobby server on a timer

    bool voiceOK = voice_init();   // mic + speaker + UDP socket

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

    while (!WindowShouldClose())
    {
        if (g_quitForUpdate && quitAt < 0) quitAt = GetTime() + 1.5;
        if (quitAt > 0 && GetTime() >= quitAt) break;

        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        // Voice: hold V to talk; always drain incoming audio.
        if (voiceOK) { voice_set_talking(IsKeyDown(KEY_V)); voice_poll(); }

        // Heartbeat + refresh online list every 2 seconds once we have a name.
        if (g_username[0] && GetTime() >= nextPoll) {
            PollLobby();
            ReadOnlineFile();
            nextPoll = GetTime() + 2.0;
        }

        int W = GetScreenWidth(), H = GetScreenHeight();
        Vector2 m = GetMousePosition();

        if (screen == SCREEN_USERNAME)
        {
            int c = GetCharPressed();
            while (c > 0) {
                size_t len = strlen(g_username);
                if (c >= 32 && c < 127 && len < sizeof(g_username) - 1) {
                    g_username[len] = (char)c;
                    g_username[len + 1] = '\0';
                }
                c = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE)) {
                size_t len = strlen(g_username);
                if (len > 0) g_username[len - 1] = '\0';
            }

            Rectangle field = { W*0.5f - 200, H*0.5f - 20, 400, 44 };
            Rectangle go    = { W*0.5f - 80,  H*0.5f + 50, 160, 44 };
            bool hoverGo = CheckCollisionPointRec(m, go);
            bool canGo   = strlen(g_username) > 0;

            bool submit = false;
            if (IsKeyPressed(KEY_ENTER) && canGo) submit = true;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hoverGo && canGo) submit = true;
            if (submit) { SaveUsername(); screen = SCREEN_DESKTOP; }

            BeginDrawing();
                ClearBackground(BLACK);
                const char *title = "Create a username";
                int ts = 30, ttw = MeasureText(title, ts);
                DrawText(title, W/2 - ttw/2, (int)(H*0.5f - 90), ts, RAYWHITE);

                DrawRectangleRounded(field, 0.25f, 6, (Color){30,30,36,255});
                DrawRectangleRoundedLines(field, 0.25f, 6, (Color){90,90,110,255});
                char shown[40];
                snprintf(shown, sizeof(shown), "%s%s", g_username,
                         (((int)(GetTime()*2)) % 2) ? "_" : "");
                DrawText(shown, (int)field.x + 14, (int)field.y + 12, 22, RAYWHITE);

                Color gbg = !canGo ? (Color){50,50,55,255}
                                   : (hoverGo ? (Color){70,140,90,255} : (Color){55,110,70,255});
                DrawRectangleRounded(go, 0.3f, 6, gbg);
                DrawRectangleRoundedLines(go, 0.3f, 6, (Color){120,170,130,255});
                const char *gt = "Continue";
                int gtw = MeasureText(gt, 20);
                DrawText(gt, (int)(go.x + go.width/2 - gtw/2), (int)(go.y + 12), 20, RAYWHITE);

                DrawText("Saved on this computer. You can change it later.",
                         W/2 - MeasureText("Saved on this computer. You can change it later.", 14)/2,
                         (int)(go.y + go.height + 16), 14, GRAY);
            EndDrawing();
        }
        else if (screen == SCREEN_DESKTOP)
        {
            Rectangle btn   = { (float)(W - 170), (float)(H - 60), 150, 40 };
            Rectangle lobby = { (float)(W - 170), (float)(H - 130), 150, 50 };

            bool hoverFolder = CheckCollisionPointRec(m,
                (Rectangle){ icon.x-10, icon.y-10, icon.width+20, icon.height+40 });
            bool hoverBtn   = CheckCollisionPointRec(m, btn);
            bool hoverLobby = CheckCollisionPointRec(m, lobby);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                if (hoverBtn) { strcpy(g_updateMsg, "Checking for updates..."); CheckForUpdate(); }
                else if (hoverLobby) { screen = SCREEN_LOBBY; }
                else if (hoverFolder) {
                    selected = true;
                    double now = GetTime();
                    if (now - lastClick <= DOUBLE_CLICK) opened = !opened;
                    lastClick = now;
                } else selected = false;
            }

            BeginDrawing();
                ClearBackground(BLACK);

                Rectangle hit = { icon.x-10, icon.y-10, icon.width+20, icon.height+40 };
                if (selected)      DrawRectangleRounded(hit, 0.15f, 6, (Color){60,90,140,120});
                else if (hoverFolder) DrawRectangleRounded(hit, 0.15f, 6, (Color){40,40,40,120});

                DrawFolder(icon, (Color){235,200,90,255}, (Color){220,185,75,255});
                int fs = 18, tw = MeasureText(label, fs);
                DrawText(label, (int)(icon.x+icon.width/2 - tw/2), (int)(icon.y+icon.height+6), fs, RAYWHITE);

                char who[48];
                snprintf(who, sizeof(who), "Signed in: %s", g_username);
                DrawText(who, W - 20 - MeasureText(who, 16), 16, 16, (Color){170,170,180,255});

                if (opened) {
                    Rectangle win = { W*0.5f-300, H*0.5f-200, 600, 400 };
                    DrawRectangleRounded(win, 0.04f, 8, (Color){25,25,28,255});
                    DrawRectangleRoundedLines(win, 0.04f, 8, (Color){80,80,90,255});
                    DrawText(label, (int)win.x+16, (int)win.y+12, 20, RAYWHITE);
                    DrawText("(empty)", (int)win.x+16, (int)win.y+50, 16, GRAY);
                }

                Color lbg = hoverLobby ? (Color){200,60,60,255} : (Color){150,40,40,255};
                DrawRectangleRounded(lobby, 0.3f, 6, lbg);
                DrawRectangleRoundedLines(lobby, 0.3f, 6, (Color){230,120,120,255});
                const char *ltxt = "Enter Lobby";
                int ltw = MeasureText(ltxt, 18);
                DrawText(ltxt, (int)(lobby.x+lobby.width/2 - ltw/2), (int)(lobby.y+16), 18, RAYWHITE);

                Color bg = hoverBtn ? (Color){70,70,80,255} : (Color){45,45,52,255};
                DrawRectangleRounded(btn, 0.3f, 6, bg);
                DrawRectangleRoundedLines(btn, 0.3f, 6, (Color){90,90,100,255});
                const char *btnTxt = "Check for Update";
                int btw = MeasureText(btnTxt, 16);
                DrawText(btnTxt, (int)(btn.x+btn.width/2 - btw/2), (int)(btn.y+12), 16, RAYWHITE);
                DrawText(APP_VERSION, (int)btn.x, (int)(btn.y-22), 14, GRAY);
                if (g_updateMsg[0]) {
                    int mw = MeasureText(g_updateMsg, 14);
                    DrawText(g_updateMsg, W-20-mw, H-160, 14, (Color){200,200,210,255});
                }
            EndDrawing();
        }
        else // SCREEN_LOBBY
        {
            t += GetFrameTime();

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

                // Count how many people are online (non-empty lines).
                int people = 0;
                {
                    for (const char *p = g_online; *p; ) {
                        const char *e = p; while (*e && *e != '\n') e++;
                        if (e > p) people++;
                        p = (*e == '\n') ? e + 1 : e;
                    }
                }

                BeginMode3D(cam);
                    DrawPlane((Vector3){0,0,0}, (Vector2){20,20}, (Color){235,235,235,255});

                    // Big red cube (the room itself / host marker)
                    DrawCube(cubePos, 1.5f, 1.5f, 1.5f, RED);
                    DrawCubeWires(cubePos, 1.5f, 1.5f, 1.5f, MAROON);

                    // One small cube per online person, in a slowly rotating ring.
                    float radius = 3.5f;
                    for (int i = 0; i < people; i++) {
                        float a = (6.2831853f * i / (people > 0 ? people : 1)) + t * 0.5f;
                        Vector3 p = {
                            cosf(a) * radius,
                            0.6f + sinf(t * 2.0f + i) * 0.15f,  // gentle bob
                            sinf(a) * radius
                        };
                        DrawCube(p, 0.7f, 0.7f, 0.7f, (Color){60,90,200,255});
                        DrawCubeWires(p, 0.7f, 0.7f, 0.7f, (Color){30,50,140,255});
                    }
                EndMode3D();

                DrawText("LOBBY", 20, H-50, 28, (Color){40,40,40,255});

                // Push-to-talk hint / indicator
                if (!voiceOK) {
                    DrawText("voice off (mic/socket failed)", 20, H-78, 16, (Color){170,90,90,255});
                } else if (voice_is_talking()) {
                    DrawCircle(40, H-92, 10, (Color){220,40,40,255});
                    DrawText("TALKING (hold V)", 60, H-100, 18, (Color){200,40,40,255});
                } else {
                    DrawText("Hold V to talk", 20, H-100, 18, (Color){90,90,90,255});
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
                    DrawText("Online", px+14, py+10, 20, (Color){40,40,40,255});

                    // draw each line
                    char buf[2048];
                    strncpy(buf, g_online, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
                    int y = py + 40;
                    int shown = 0;
                    char *line = strtok(buf, "\n");
                    while (line) {
                        if (line[0]) {
                            bool me = (strcmp(line, g_username) == 0);
                            DrawText(me ? TextFormat("%s (you)", line) : line,
                                     px+18, y, 16, me ? (Color){40,110,60,255} : (Color){60,60,60,255});
                            y += 22; shown++;
                        }
                        line = strtok(NULL, "\n");
                    }
                    if (shown == 0)
                        DrawText("(connecting...)", px+18, y, 14, GRAY);
                }
                DrawText(TextFormat("server: %s", g_serverURL), 20, H-20, 14, GRAY);

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
                    DrawText("Chat", (int)bx+14, (int)by+8, 18, (Color){60,60,70,255});

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
                        DrawText(lines[i % 64], (int)bx+14, yy, 14, (Color){50,50,60,255});
                        yy += 20;
                    }
                    if (nl == 0) DrawText("(no messages yet)", (int)bx+14, yy, 14, GRAY);

                    // input row
                    if (typing) {
                        DrawRectangle((int)bx+10, (int)by+bh-30, bw-20, 24, (Color){235,238,245,255});
                        DrawText(TextFormat("%s_", chatInput), (int)bx+14, (int)by+bh-26, 14, (Color){20,20,30,255});
                    } else {
                        DrawText("Press T to chat", (int)bx+14, (int)by+bh-26, 14, (Color){130,130,140,255});
                    }
                }

                Color bbg = hoverBack ? (Color){200,200,200,255} : (Color){220,220,220,255};
                DrawRectangleRounded(back, 0.3f, 6, bbg);
                DrawRectangleRoundedLines(back, 0.3f, 6, (Color){150,150,150,255});
                DrawText("< Back", 34, 28, 18, (Color){40,40,40,255});
            EndDrawing();
        }
    }

    voice_shutdown();
    CloseWindow();
    return 0;
}
