// main.c — Black desktop: folder icon, Update button, and a Lobby room.
// Cross-platform: macOS + Windows. Built with raylib.
//
// Screens:
//   DESKTOP : black screen, a folder (top-left), an Update button + a "Lobby"
//             entry icon (bottom-right).
//   LOBBY   : white room with a floating red cube. ESC/Back returns to desktop.
//
// Networking is NOT wired up yet — the lobby is a local mockup. The server and
// connection come next.
//
// Press ESC: in lobby -> back to desktop; on desktop -> quit.

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ---- Bump this every time you publish a new GitHub release ----
#define APP_VERSION "v0.1.0"
#define GH_OWNER    "everydaymighty"
#define GH_REPO     "desk"

typedef enum { SCREEN_DESKTOP, SCREEN_LOBBY } Screen;

static char g_updateMsg[256] = "";

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

// Name of the release asset to download for each platform. These must match
// the file names you attach to the GitHub release.
#if defined(_WIN32)
  #define ASSET_NAME "black.exe"
#else
  #define ASSET_NAME "black"
#endif

// Fully in-app update: download the new binary next to the running one, then a
// small helper swaps it in and relaunches. No browser is ever opened.
static void DoInAppUpdate(const char *latest)
{
    const char *dlBase =
        "https://github.com/" GH_OWNER "/" GH_REPO "/releases/latest/download/";

#if defined(_WIN32)
    // Download to black.new.exe, then a detached PowerShell waits for us to
    // exit, replaces black.exe, and relaunches it.
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "powershell -NoProfile -Command "
        "\"$ErrorActionPreference='Stop'; "
        "$exe=(Get-Process -Id $PID).Path; "  // not used directly; placeholder
        "Invoke-WebRequest -UseBasicParsing -Uri '%s%s' -OutFile 'black.new.exe'\"",
        dlBase, ASSET_NAME);
    char out[256];
    if (!RunCapture(cmd, out, sizeof(out))) { /* ignore */ }

    // Launch detached swapper: wait for this process to close, swap, relaunch.
    char swap[2048];
    snprintf(swap, sizeof(swap),
        "powershell -NoProfile -WindowStyle Hidden -Command "
        "\"Start-Process powershell -WindowStyle Hidden -ArgumentList "
        "'-NoProfile','-Command','Start-Sleep -Seconds 2; "
        "Move-Item -Force black.new.exe black.exe; Start-Process black.exe'\"");
    system(swap);
    snprintf(g_updateMsg, sizeof(g_updateMsg),
             "Updating to %s... the app will relaunch.", latest);
#else
    // macOS/Linux: download to black.new, mark executable, then a detached
    // shell waits, replaces ./black, and relaunches it.
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "curl -fsSL -H 'User-Agent: desk' -o black.new '%s%s' && chmod +x black.new",
        dlBase, ASSET_NAME);
    char out[256];
    if (!RunCapture(cmd, out, sizeof(out))) { /* ignore */ }

    // Detached swapper.
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
        g_quitForUpdate = true;   // close so the helper can swap the file
    }
}

int main(void)
{
    InitWindow(0, 0, "Desktop");
    int mon = GetCurrentMonitor();
    SetWindowSize(GetMonitorWidth(mon), GetMonitorHeight(mon));
    ToggleFullscreen();
    SetTargetFPS(60);

    Screen screen = SCREEN_DESKTOP;

    // Desktop folder
    const char *label = "Folder";
    Rectangle icon = { 80, 80, 90, 70 };
    bool selected = false;
    double lastClick = -1.0;
    const double DOUBLE_CLICK = 0.35;
    bool opened = false;

    // 3D camera for the lobby
    Camera3D cam = { 0 };
    cam.position   = (Vector3){ 6.0f, 5.0f, 6.0f };
    cam.target     = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.fovy       = 50.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    float t = 0.0f;  // animation time for the floating cube
    double quitAt = -1.0;  // when set, close shortly after to allow the updater to run

    while (!WindowShouldClose())
    {
        // If an update started, show the message briefly, then exit so the
        // helper can swap the file and relaunch.
        if (g_quitForUpdate && quitAt < 0) quitAt = GetTime() + 1.5;
        if (quitAt > 0 && GetTime() >= quitAt) break;

        int W = GetScreenWidth(), H = GetScreenHeight();
        Vector2 m = GetMousePosition();

        if (screen == SCREEN_DESKTOP)
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

                if (opened) {
                    Rectangle win = { W*0.5f-300, H*0.5f-200, 600, 400 };
                    DrawRectangleRounded(win, 0.04f, 8, (Color){25,25,28,255});
                    DrawRectangleRoundedLines(win, 0.04f, 8, (Color){80,80,90,255});
                    DrawText(label, (int)win.x+16, (int)win.y+12, 20, RAYWHITE);
                    DrawText("(empty)", (int)win.x+16, (int)win.y+50, 16, GRAY);
                }

                // Lobby entry (bottom-right, above the update button)
                Color lbg = hoverLobby ? (Color){200,60,60,255} : (Color){150,40,40,255};
                DrawRectangleRounded(lobby, 0.3f, 6, lbg);
                DrawRectangleRoundedLines(lobby, 0.3f, 6, (Color){230,120,120,255});
                const char *ltxt = "Enter Lobby";
                int ltw = MeasureText(ltxt, 18);
                DrawText(ltxt, (int)(lobby.x+lobby.width/2 - ltw/2), (int)(lobby.y+16), 18, RAYWHITE);

                // Update button
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
            if (IsKeyPressed(KEY_ESCAPE)) { screen = SCREEN_DESKTOP; }

            // Back button (top-left)
            Rectangle back = { 20, 20, 90, 36 };
            bool hoverBack = CheckCollisionPointRec(m, back);
            if (hoverBack && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) screen = SCREEN_DESKTOP;

            // Floating + spinning red cube
            Vector3 cubePos = { 0.0f, 1.5f + sinf(t * 1.5f) * 0.5f, 0.0f };

            BeginDrawing();
                ClearBackground(RAYWHITE);   // white room

                BeginMode3D(cam);
                    DrawPlane((Vector3){0,0,0}, (Vector2){20,20}, (Color){235,235,235,255}); // floor
                    DrawGrid(20, 1.0f);
                    DrawCube(cubePos, 1.5f, 1.5f, 1.5f, RED);
                    DrawCubeWires(cubePos, 1.5f, 1.5f, 1.5f, MAROON);
                EndMode3D();

                DrawText("LOBBY", 20, H-50, 28, (Color){40,40,40,255});
                DrawText("(networking not connected yet)", 20, H-20, 14, GRAY);

                Color bbg = hoverBack ? (Color){200,200,200,255} : (Color){220,220,220,255};
                DrawRectangleRounded(back, 0.3f, 6, bbg);
                DrawRectangleRoundedLines(back, 0.3f, 6, (Color){150,150,150,255});
                DrawText("< Back", 34, 28, 18, (Color){40,40,40,255});
            EndDrawing();
        }
    }

    CloseWindow();
    return 0;
}
