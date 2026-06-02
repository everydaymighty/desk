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

// ---- Prefer the dedicated GPU on dual-graphics laptops ----
// These exported symbols tell NVIDIA Optimus / AMD switchable graphics to run
// this app on the high-performance discrete GPU. No windows.h needed — just the
// exported globals, which the driver reads from the executable.
#if defined(_WIN32)
  __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
  __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

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

// ---- Accent palette (from the user's swatches) ----
#define ACC_BLUE   (Color){ 110, 140, 190, 255 }   // muted blue
#define ACC_OLIVE  (Color){ 120, 130,  80, 255 }   // olive/khaki
#define ACC_MAROON (Color){ 150,  80,  80, 255 }   // dusty maroon
#define ACC_GREEN  (Color){ 110, 140, 100, 255 }   // sage green
#define ACC_PURPLE (Color){ 150, 130, 175, 255 }   // muted lavender
#define ACC_TAN    (Color){ 190, 170, 135, 255 }   // tan

// A flat, sharp-edged, hover-aware button (squared corners for a cleaner look).
static bool Button(Rectangle r, const char *label, int fontSize,
                   Color base, Color hover, Color textCol, Vector2 mouse) {
    bool over = CheckCollisionPointRec(mouse, r);
    Color c = over ? hover : base;
    DrawRectangleRounded(r, 0.04f, 4, c);
    DrawRectangleRoundedLines(r, 0.04f, 4, over ? (Color){130,135,150,220} : (Color){70,72,84,160});
    int tw = TxtW(label, fontSize);
    Txt(label, (int)(r.x + r.width/2 - tw/2), (int)(r.y + r.height/2 - fontSize/2), fontSize, textCol);
    return over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Horizontal slider 0..maxVal. Drag or click the track to set. Returns value.
static int Slider(Rectangle r, int value, int maxVal, Vector2 mouse) {
    DrawRectangle((int)r.x, (int)(r.y + r.height/2 - 2), (int)r.width, 4, (Color){55,57,68,255});
    float frac = (float)value / (float)maxVal;
    float kx = r.x + frac * r.width;
    // filled portion (accent blue)
    DrawRectangle((int)r.x, (int)(r.y + r.height/2 - 2), (int)(kx - r.x), 4, ACC_BLUE);
    // square knob
    DrawRectangle((int)kx-5, (int)(r.y + r.height/2)-7, 10, 14, (Color){175,190,215,255});
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

typedef enum { SCREEN_INTRO, SCREEN_FLASH, SCREEN_USERNAME, SCREEN_DESKTOP, SCREEN_LOBBY, SCREEN_GAME } Screen;

// ---- Level editor: placeable primitive entities ----
typedef enum { PROP_BOX, PROP_CYLINDER, PROP_SPHERE, PROP_PEDESTAL, PROP_WALL, PROP_RAMP, PROP_TYPE_COUNT } PropType;
static const char *PROP_NAMES[PROP_TYPE_COUNT] = { "Box", "Cylinder", "Sphere", "Pedestal", "Wall", "Ramp" };
typedef struct {
    PropType type;
    Vector3  pos;
    Vector3  size;   // w,h,d (radius uses x)
    float    rotY;   // yaw degrees
    Color    color;
    int      texId;  // index into g_texLib, or -1 for none (flat color)
} Prop;
#define MAX_PROPS 256
static Prop  g_props[MAX_PROPS];
static int   g_propCount = 0;

// ---- Texture library: imported images you can assign to props ----
#define MAX_TEX 32
static Texture2D g_texLib[MAX_TEX];
static char      g_texName[MAX_TEX][64];
static int       g_texCount = 0;
// Scan the textures/ folder for images and load them into the library.
static void LoadTextureLibrary(void) {
    g_texCount = 0;
    FilePathList files = LoadDirectoryFilesEx("textures", ".png;.jpg;.jpeg;.bmp", false);
    for (unsigned int i=0; i<files.count && g_texCount<MAX_TEX; i++) {
        Texture2D t = LoadTexture(files.paths[i]);
        if (t.id != 0) {
            SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
            g_texLib[g_texCount] = t;
            const char *nm = GetFileName(files.paths[i]);
            strncpy(g_texName[g_texCount], nm, 63); g_texName[g_texCount][63]='\0';
            g_texCount++;
        }
    }
    UnloadDirectoryFiles(files);
}

// Palette of colors to cycle when placing.
static Color PROP_PALETTE[8] = {
    {200,200,206,255},{120,150,190,255},{190,150,110,255},{150,170,130,255},
    {200,120,120,255},{170,140,190,255},{210,200,150,255},{90,95,110,255}
};

static void SaveMap(void) {
    FILE *f = fopen("map.txt","w");
    if (!f) return;
    fprintf(f, "# desk map: type x y z sx sy sz rotY r g b texId\n");
    for (int i=0;i<g_propCount;i++) {
        Prop *p=&g_props[i];
        fprintf(f, "%d %.2f %.2f %.2f %.2f %.2f %.2f %.1f %d %d %d %d\n",
            p->type, p->pos.x,p->pos.y,p->pos.z, p->size.x,p->size.y,p->size.z,
            p->rotY, p->color.r,p->color.g,p->color.b, p->texId);
    }
    fclose(f);
}
static void LoadMap(void) {
    FILE *f = fopen("map.txt","r");
    if (!f) return;
    g_propCount = 0;
    char line[256];
    while (g_propCount<MAX_PROPS && fgets(line,sizeof(line),f)) {
        if (line[0]=='#'||line[0]=='\n') continue;
        Prop p; int t,r,g,b,tex=-1;
        int got = sscanf(line,"%d %f %f %f %f %f %f %f %d %d %d %d",
            &t,&p.pos.x,&p.pos.y,&p.pos.z,&p.size.x,&p.size.y,&p.size.z,&p.rotY,&r,&g,&b,&tex);
        if (got>=11) {
            p.type=(PropType)t; p.color=(Color){(unsigned char)r,(unsigned char)g,(unsigned char)b,255};
            p.texId = (got>=12) ? tex : -1;
            g_props[g_propCount++]=p;
        }
    }
    fclose(f);
}
// Textured cube (uses the assigned texture on all faces). Falls back handled by caller.
static void DrawCubeTex(Texture2D tex, Vector3 size, Color tint) {
    float x=size.x/2, y=size.y/2, z=size.z/2;
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r,tint.g,tint.b,tint.a);
    // front
    rlNormal3f(0,0,1);
    rlTexCoord2f(0,0);rlVertex3f(-x,-y,z); rlTexCoord2f(1,0);rlVertex3f(x,-y,z);
    rlTexCoord2f(1,1);rlVertex3f(x,y,z);   rlTexCoord2f(0,1);rlVertex3f(-x,y,z);
    // back
    rlNormal3f(0,0,-1);
    rlTexCoord2f(1,0);rlVertex3f(-x,-y,-z); rlTexCoord2f(1,1);rlVertex3f(-x,y,-z);
    rlTexCoord2f(0,1);rlVertex3f(x,y,-z);   rlTexCoord2f(0,0);rlVertex3f(x,-y,-z);
    // top
    rlNormal3f(0,1,0);
    rlTexCoord2f(0,1);rlVertex3f(-x,y,-z); rlTexCoord2f(0,0);rlVertex3f(-x,y,z);
    rlTexCoord2f(1,0);rlVertex3f(x,y,z);   rlTexCoord2f(1,1);rlVertex3f(x,y,-z);
    // bottom
    rlNormal3f(0,-1,0);
    rlTexCoord2f(1,1);rlVertex3f(-x,-y,-z); rlTexCoord2f(0,1);rlVertex3f(x,-y,-z);
    rlTexCoord2f(0,0);rlVertex3f(x,-y,z);   rlTexCoord2f(1,0);rlVertex3f(-x,-y,z);
    // right
    rlNormal3f(1,0,0);
    rlTexCoord2f(1,0);rlVertex3f(x,-y,-z); rlTexCoord2f(1,1);rlVertex3f(x,y,-z);
    rlTexCoord2f(0,1);rlVertex3f(x,y,z);   rlTexCoord2f(0,0);rlVertex3f(x,-y,z);
    // left
    rlNormal3f(-1,0,0);
    rlTexCoord2f(0,0);rlVertex3f(-x,-y,-z); rlTexCoord2f(1,0);rlVertex3f(-x,-y,z);
    rlTexCoord2f(1,1);rlVertex3f(-x,y,z);   rlTexCoord2f(0,1);rlVertex3f(-x,y,-z);
    rlEnd();
    rlSetTexture(0);
}

static void DrawProp(Prop *p, Color override, bool useOverride) {
    Color c = useOverride ? override : p->color;
    int hasTex = (!useOverride && p->texId>=0 && p->texId<g_texCount);
    rlPushMatrix();
    rlTranslatef(p->pos.x, p->pos.y, p->pos.z);
    rlRotatef(p->rotY, 0,1,0);
    switch (p->type) {
        case PROP_BOX: case PROP_WALL: case PROP_PEDESTAL: case PROP_RAMP:
            if (hasTex) DrawCubeTex(g_texLib[p->texId], p->size, WHITE);
            else { DrawCube((Vector3){0,0,0}, p->size.x,p->size.y,p->size.z, c);
                   DrawCubeWires((Vector3){0,0,0}, p->size.x,p->size.y,p->size.z, Fade(BLACK,0.2f)); }
            break;
        case PROP_CYLINDER: DrawCylinder((Vector3){0,-p->size.y/2,0}, p->size.x,p->size.x,p->size.y,16,c); break;
        case PROP_SPHERE:   DrawSphere((Vector3){0,0,0}, p->size.x, c); break;
        default: break;
    }
    rlPopMatrix();
}
// Draw a textured quad (for floor/walls). Center c, with half-extents along two
// axes given by 'right' and 'up' vectors; 'tile' repeats the texture.
static void DrawTexturedQuad(Texture2D tex, Vector3 c, Vector3 right, Vector3 up, float tile) {
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255,255,255,255);
    Vector3 p0={c.x-right.x-up.x, c.y-right.y-up.y, c.z-right.z-up.z};
    Vector3 p1={c.x+right.x-up.x, c.y+right.y-up.y, c.z+right.z-up.z};
    Vector3 p2={c.x+right.x+up.x, c.y+right.y+up.y, c.z+right.z+up.z};
    Vector3 p3={c.x-right.x+up.x, c.y-right.y+up.y, c.z-right.z+up.z};
    rlTexCoord2f(0,0);     rlVertex3f(p0.x,p0.y,p0.z);
    rlTexCoord2f(tile,0);  rlVertex3f(p1.x,p1.y,p1.z);
    rlTexCoord2f(tile,tile);rlVertex3f(p2.x,p2.y,p2.z);
    rlTexCoord2f(0,tile);  rlVertex3f(p3.x,p3.y,p3.z);
    rlEnd();
    rlSetTexture(0);
}

static Vector3 DefaultSize(PropType t) {
    switch(t){
        case PROP_CYLINDER: return (Vector3){0.5f,2.0f,0.5f};
        case PROP_SPHERE:   return (Vector3){0.6f,0.6f,0.6f};
        case PROP_PEDESTAL: return (Vector3){1.4f,1.2f,1.4f};
        case PROP_WALL:     return (Vector3){4.0f,3.0f,0.3f};
        case PROP_RAMP:     return (Vector3){2.0f,0.3f,3.0f};
        default:            return (Vector3){1.0f,1.0f,1.0f};
    }
}

static char g_updateMsg[256] = "";

// ---- Username, saved locally next to the exe (no network) ----
#define USER_FILE "desk_user.txt"
static char g_username[32] = "";

// ---- Lobby networking (HTTP polling) ----
// Server base URL is read from desk_server.txt (e.g. http://localhost:8080
// for local testing, or your ngrok https URL for other computers). If the file
// is missing we default to localhost.
// NOTE: auth was merged into the main lobby server (g_serverURL, port 8080), so
// there is no separate auth URL anymore. desk_auth.txt is no longer read.
#define SERVER_FILE "desk_server.txt"
#define ONLINE_FILE "desk_online.txt"
#define CHAT_FILE   "desk_chat.txt"
#define FRIENDS_FILE "desk_friends.txt"
static char g_serverURL[256] = "http://localhost:8080";
static char g_token[512]     = "";   // reserved for a future session token
static char g_online[2048]   = "";   // raw newline-separated names from server
static char g_chat[4096]     = "";   // raw newline-separated chat lines from server
static char g_friends[2048]  = "";   // raw newline-separated friend names
#define GAMESTATE_FILE "desk_game.txt"
#define LEADERBOARD_FILE "desk_lb.txt"
static char g_gamestate[512] = "";   // raw JSON match state from server
static char g_leaderboard[1024] = "";// raw "name W-L" lines
static int  g_myRole = -1;           // -1 unknown, 0/1 = player slot, 2 = spectator

// Forward declarations (defined later, used above their definitions).
static int RunCapture(const char *cmd, char *out, size_t outSize);
static void UrlEncode(const char *in, char *out, size_t outSize);

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
    char un[128]; UrlEncode(g_username, un, sizeof(un));   // never inject names
#if defined(_WIN32)
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" \"%s/hello?name=%s\" >NUL 2>&1",
        g_serverURL, un);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" -o \"%s\" \"%s/online\" 2>NUL",
        ONLINE_FILE, g_serverURL);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" -o \"%s\" \"%s/chat\" 2>NUL",
        CHAT_FILE, g_serverURL);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" -o \"%s\" \"%s/friends?name=%s\" 2>NUL",
        FRIENDS_FILE, g_serverURL, un);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" -o \"%s\" \"%s/leaderboard\" 2>NUL",
        LEADERBOARD_FILE, g_serverURL);
    system(cmd);
#else
    snprintf(cmd, sizeof(cmd),
        "( curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" \"%s/hello?name=%s\" >/dev/null 2>&1; "
        "  curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" -o \"%s\" \"%s/online\" 2>/dev/null; "
        "  curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" -o \"%s\" \"%s/chat\" 2>/dev/null; "
        "  curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" -o \"%s\" \"%s/friends?name=%s\" 2>/dev/null; "
        "  curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" -o \"%s\" \"%s/leaderboard\" 2>/dev/null ) &",
        g_serverURL, un, ONLINE_FILE, g_serverURL, CHAT_FILE, g_serverURL,
        FRIENDS_FILE, g_serverURL, un, LEADERBOARD_FILE, g_serverURL);
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
    char un[128], fn[128];
    UrlEncode(g_username, un, sizeof(un));
    UrlEncode(friendName, fn, sizeof(fn));
#if defined(_WIN32)
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" \"%s/addfriend?name=%s&friend=%s\" >NUL 2>&1",
        g_serverURL, un, fn);
#else
    snprintf(cmd, sizeof(cmd),
        "curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" \"%s/addfriend?name=%s&friend=%s\" >/dev/null 2>&1 &",
        g_serverURL, un, fn);
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

// Is this exact name present in the online list? Line-exact (not substring) so
// "Al" doesn't falsely match "Alice".
static int NameOnline(const char *name)
{
    char buf[2048]; strncpy(buf, g_online, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
    char *ln = strtok(buf, "\n");
    while (ln) { if (strcmp(ln, name) == 0) return 1; ln = strtok(NULL, "\n"); }
    return 0;
}

// --- Game matchmaking helpers ---
// Claim a slot (or spectator). Reads back "PLAYER 0/1" or "SPECTATOR".
static void GameJoin(void)
{
    char cmd[1024], out[64];
    char un[128]; UrlEncode(g_username, un, sizeof(un));
    snprintf(cmd, sizeof(cmd), "curl -s -m 5 -H \"ngrok-skip-browser-warning: 1\" \"%s/join?name=%s\"", g_serverURL, un);
    if (RunCapture(cmd, out, sizeof(out))) {
        if (strncmp(out, "PLAYER 0", 8) == 0) g_myRole = 0;
        else if (strncmp(out, "PLAYER 1", 8) == 0) g_myRole = 1;
        else if (strncmp(out, "SPECTATOR", 9) == 0) g_myRole = 2;
    }
}
// Refresh the match state JSON in g_gamestate. The fetch is detached so it never
// blocks the render thread; we READ the file FIRST (the result of the previous
// call's fetch) and THEN kick off the next fetch. This means the state is one
// poll cycle (~0.5s) behind, which is fine — the old code read the file in the
// same call it spawned the async write, so it always saw stale/empty data anyway.
static void GamePoll(void)
{
    // 1) consume whatever the last fetch wrote
    FILE *fp = fopen(GAMESTATE_FILE, "r");
    if (fp) { size_t n = fread(g_gamestate,1,sizeof(g_gamestate)-1,fp); g_gamestate[n]='\0'; fclose(fp); }
    // 2) kick off the next fetch (detached)
    char cmd[1024];
#if defined(_WIN32)
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" -o \"%s\" \"%s/gamestate\" 2>NUL", GAMESTATE_FILE, g_serverURL);
#else
    snprintf(cmd, sizeof(cmd),
        "curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" -o \"%s\" \"%s/gamestate\" 2>/dev/null &", GAMESTATE_FILE, g_serverURL);
#endif
    system(cmd);
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
    char un[128];  UrlEncode(g_username, un, sizeof(un));
    char cmd[1024];
#if defined(_WIN32)
    snprintf(cmd, sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" \"%s/say?name=%s&msg=%s\" >NUL 2>&1",
        g_serverURL, un, enc);
#else
    snprintf(cmd, sizeof(cmd),
        "curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" \"%s/say?name=%s&msg=%s\" >/dev/null 2>&1 &",
        g_serverURL, un, enc);
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

// Call /login or /register on the lobby server using a POST body so the password
// never appears in the URL or process args. Writes the password to a temp file
// and has curl send it, then deletes it. Returns 1 on success, 0 on failure.
static int AuthRequest(const char *action, const char *name, const char *pw,
                       char *errOut, size_t errSize)
{
    // Auth lives on the SAME lobby server (g_serverURL, port 8080) — one server,
    // one ngrok tunnel. The password is POSTed from a temp file via curl's
    // --data-urlencode pw@FILE, so it never appears in the URL OR in the process
    // argument list (where `ps` could read it). The file is deleted immediately.
    char pwPath[512];
#if defined(_WIN32)
    const char *tmpdir = getenv("TEMP"); if (!tmpdir) tmpdir = ".";
    snprintf(pwPath, sizeof(pwPath), "%s\\desk_pw_%lu.tmp", tmpdir, (unsigned long)(GetTime()*1000));
#else
    const char *tmpdir = getenv("TMPDIR"); if (!tmpdir) tmpdir = "/tmp";
    snprintf(pwPath, sizeof(pwPath), "%s/desk_pw_%d.tmp", tmpdir, (int)(GetTime()*1000));
#endif
    FILE *pf = fopen(pwPath, "w");
    if (!pf) { snprintf(errOut, errSize, "Could not write temp file"); return 0; }
    fputs(pw, pf);   // file holds ONLY the raw password; curl reads it via pw@FILE
    fclose(pf);

    // 'name' is already restricted to [A-Za-z0-9_-] (safe in the shell and a
    // no-op under URL-encoding), and curl's --data-urlencode encodes it again, so
    // we pass it raw rather than pre-encoding (which would double-encode).
    char cmd[1024], out[1024];
    snprintf(cmd, sizeof(cmd),
        "curl -s -m 8 -H \"ngrok-skip-browser-warning: 1\" "
        "--data-urlencode \"name=%s\" --data-urlencode \"pw@%s\" \"%s/%s\"",
        name, pwPath, g_serverURL, action);
    int ok = RunCapture(cmd, out, sizeof(out));
    remove(pwPath);   // wipe the password file no matter what

    if (!ok || out[0] == '\0') {
        snprintf(errOut, errSize, "No response (is the server running?)");
        return 0;
    }
    if (strncmp(out, "OK", 2) == 0) return 1;
    const char *msg = (strncmp(out, "ERR", 3) == 0) ? out + 4 : out;
    snprintf(errOut, errSize, "%s", msg);
    return 0;
}

#if defined(_WIN32)
  #define ASSET_NAME "black.exe"
#else
  #define ASSET_NAME "black"
#endif

// Pull the first run of >=64 hex chars out of arbitrary text (certutil/shasum
// output, or a .sha256 file), lowercased, into out65. Returns 1 on success.
static int ExtractSha256(const char *in, char *out65)
{
    int run = 0; const char *start = NULL;
    for (const char *p = in; ; p++) {
        char c = *p;
        int isHex = (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
        if (isHex) { if (run==0) start = p; if (++run >= 64) break; }
        else { run = 0; start = NULL; if (c=='\0') break; }
    }
    if (run < 64 || !start) { out65[0]='\0'; return 0; }
    for (int i=0;i<64;i++) { char c=start[i]; out65[i] = (c>='A'&&c<='F') ? (char)(c-'A'+'a') : c; }
    out65[64]='\0';
    return 1;
}

// Download the published SHA-256 for ASSET_NAME and the freshly-downloaded
// binary's own hash, and compare. Returns 1 only if they match. Fails CLOSED:
// any download error, missing checksum, or mismatch returns 0 so we never
// install an unverified or tampered binary.
static int VerifyDownload(const char *dlBase, const char *localFile)
{
    char cmd[2048], out[2048];
    char expected[65] = "", actual[65] = "";

    // 1) expected hash, published alongside the release as ASSET_NAME.sha256
    snprintf(cmd, sizeof(cmd),
        "curl -fsSL -H \"User-Agent: desk\" \"%s%s.sha256\"", dlBase, ASSET_NAME);
    if (!RunCapture(cmd, out, sizeof(out)) || !ExtractSha256(out, expected)) return 0;

    // 2) actual hash of what we just downloaded
#if defined(_WIN32)
    snprintf(cmd, sizeof(cmd), "certutil -hashfile \"%s\" SHA256", localFile);
#else
    snprintf(cmd, sizeof(cmd), "shasum -a 256 \"%s\"", localFile);
#endif
    if (!RunCapture(cmd, out, sizeof(out)) || !ExtractSha256(out, actual)) return 0;

    return strcmp(expected, actual) == 0;
}

// Returns 1 if an update was downloaded, verified, and scheduled to install;
// 0 if anything failed (and nothing was replaced).
static int DoInAppUpdate(const char *latest)
{
    const char *dlBase =
        "https://github.com/" GH_OWNER "/" GH_REPO "/releases/latest/download/";
    char cmd[2048], out[256];
#if defined(_WIN32)
    const char *newFile = "black.new.exe";
    snprintf(cmd, sizeof(cmd),
        "powershell -NoProfile -Command "
        "\"Invoke-WebRequest -UseBasicParsing -Uri '%s%s' -OutFile '%s'\"",
        dlBase, ASSET_NAME, newFile);
    RunCapture(cmd, out, sizeof(out));
    if (!VerifyDownload(dlBase, newFile)) {
        remove(newFile);
        snprintf(g_updateMsg, sizeof(g_updateMsg),
                 "Update aborted: checksum did not verify.");
        return 0;
    }
    system("powershell -NoProfile -WindowStyle Hidden -Command "
           "\"Start-Process powershell -WindowStyle Hidden -ArgumentList "
           "'-NoProfile','-Command','Start-Sleep -Seconds 2; "
           "Move-Item -Force black.new.exe black.exe; Start-Process black.exe'\"");
#else
    const char *newFile = "black.new";
    snprintf(cmd, sizeof(cmd),
        "curl -fsSL -H 'User-Agent: desk' -o %s '%s%s' && chmod +x %s",
        newFile, dlBase, ASSET_NAME, newFile);
    RunCapture(cmd, out, sizeof(out));
    if (!VerifyDownload(dlBase, newFile)) {
        remove(newFile);
        snprintf(g_updateMsg, sizeof(g_updateMsg),
                 "Update aborted: checksum did not verify.");
        return 0;
    }
    system("( sleep 2; mv -f black.new black; ./black ) >/dev/null 2>&1 &");
#endif
    snprintf(g_updateMsg, sizeof(g_updateMsg),
             "Updating to %s... the app will relaunch.", latest);
    return 1;
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
        // Only relaunch if the new binary downloaded AND its checksum verified.
        if (DoInAppUpdate(latest)) g_quitForUpdate = true;
    }
}

int main(void)
{
    // Open a small window first so we can read the monitor's native resolution,
    // then resize to fill it. The user can change resolution later in Settings.
    // 4x MSAA antialiasing + resizable. (High-DPI flag left OFF: it rescales
    // mouse coordinates and would misalign every button click in the UI.)
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Desktop");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);   // don't let ESC close the app; we use it for navigation
    LoadUIFont();

    // Detect native resolution and start at it (windowed, centered).
    int mon = GetCurrentMonitor();
    int nativeW = GetMonitorWidth(mon), nativeH = GetMonitorHeight(mon);
    if (nativeW < 640 || nativeH < 480) { nativeW = 1280; nativeH = 720; } // fallback
    // Preset resolutions to cycle in Settings (last = Native).
    int resW[6] = { 1280, 1920, 2560, 3840, nativeW, nativeW };
    int resH[6] = {  720, 1080, 1440, 2160, nativeH, nativeH };
    const char *resLabel[6] = { "1280x720", "1920x1080", "2560x1440 (2K)", "3840x2160 (4K)", "Native", "Native" };
    int resMode = 4;  // default to Native
    int resCount = 5; // (6th slot is a duplicate guard; cycle through 5)
    SetWindowSize(nativeW, nativeH);
    SetWindowPosition( (GetMonitorWidth(mon)-nativeW)/2, (GetMonitorHeight(mon)-nativeH)/2 );

    // Always start at the login screen. We pre-fill the last username for
    // convenience, but never store the password, so the user logs in each run.
    LoadUsername();
    Screen screen = SCREEN_INTRO;   // walk through the door first

    // Intro (Doom-style) state — long hallway, door at the far end (-Z)
    Vector3 introPos = { 0, 1.6f, 34 };   // start way back
    float introYaw = -1.5708f;             // facing -Z (toward the door)
    float introPitch = 0;
    double flashStart = 0;

    // ---- Optional 3D model loading (.obj / .glb / .gltf) ----
    // Drop a model file in the folder and name it in models.txt (one path per
    // line, optional "x y z scale" after). If models.txt is missing, none load.
    #define MAX_MODELS 32
    Model  loadedModels[MAX_MODELS];
    Vector3 modelPos[MAX_MODELS];
    float  modelScale[MAX_MODELS];
    char   modelPath[MAX_MODELS][200];
    int    modelCount = 0;
    {
        FILE *mf = fopen("models.txt", "r");
        if (mf) {
            char line[256];
            while (modelCount < MAX_MODELS && fgets(line, sizeof(line), mf)) {
                size_t n = strlen(line);
                while (n>0 && (line[n-1]=='\n'||line[n-1]=='\r')) line[--n]='\0';
                if (line[0]=='\0' || line[0]=='#') continue;
                char path[200]; float px=0,py=0,pz=10,sc=1.0f;
                int got = sscanf(line, "%199s %f %f %f %f", path, &px,&py,&pz,&sc);
                if (got >= 1 && FileExists(path)) {
                    loadedModels[modelCount] = LoadModel(path);
                    strncpy(modelPath[modelCount], path, 199); modelPath[modelCount][199]='\0';
                    modelPos[modelCount]   = (Vector3){ px, py, pz };
                    modelScale[modelCount] = (got >= 5) ? sc : 1.0f;
                    modelCount++;
                }
            }
            fclose(mf);
        }
    }
    // ---- Museum textures (optional: drop these .png files in the folder) ----
    // floor.png, wall.png, ceiling.png, art1.png, art2.png. Missing ones just
    // fall back to flat colors, so it never breaks.
    Texture2D texFloor={0}, texWall={0}, texCeil={0}, texArt1={0}, texArt2={0};
    bool hasFloor=false, hasWall=false, hasCeil=false, hasArt1=false, hasArt2=false;
    if (FileExists("floor.png"))   { texFloor=LoadTexture("floor.png");   hasFloor=(texFloor.id!=0); }
    if (FileExists("wall.png"))    { texWall =LoadTexture("wall.png");    hasWall =(texWall.id!=0); }
    if (FileExists("ceiling.png")) { texCeil =LoadTexture("ceiling.png"); hasCeil =(texCeil.id!=0); }
    if (FileExists("art1.png"))    { texArt1 =LoadTexture("art1.png");    hasArt1 =(texArt1.id!=0); }
    if (FileExists("art2.png"))    { texArt2 =LoadTexture("art2.png");    hasArt2 =(texArt2.id!=0); }
    // make textures tile/repeat where used
    if (hasFloor) SetTextureWrap(texFloor, TEXTURE_WRAP_REPEAT);
    if (hasWall)  SetTextureWrap(texWall,  TEXTURE_WRAP_REPEAT);

    // ---- Editor state ----
    bool editMode = false;
    int  editSel = -1;        // selected loaded-model index (legacy), -1 = none
    LoadTextureLibrary();     // scan textures/ folder for imported images
    LoadMap();                // load placed props from map.txt
    int  texSel = -1;         // texture-library index chosen in the editor panel
    int  propSel = -1;        // selected prop index
    int  paletteType = 0;     // currently chosen prop type to spawn
    int  paletteColor = 0;    // currently chosen color index
    bool gridSnap = true;     // snap placement to 0.5 grid
    LoadServerURL();
    double nextPoll = 0.0;   // poll the lobby server on a timer
    double nextGamePoll = 0.0; // poll /gamestate while in a match

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

        if (screen == SCREEN_INTRO)
        {
            float dt = GetFrameTime();
            static bool introCursorInit = false;
            if (!introCursorInit) { if (!editMode) DisableCursor(); introCursorInit = true; }

            // E toggles the level editor (god-mode free-fly)
            if (IsKeyPressed(KEY_E)) {
                editMode = !editMode; editSel = -1;
                if (editMode) EnableCursor();   // editor: free cursor for clicking
                else          DisableCursor();  // walk: locked mouse-look
            }

            if (!editMode) {
                // ---- WALK mode: mouse-look, floor-locked, bounded hallway ----
                Vector2 md = GetMouseDelta();
                introYaw += md.x * 0.003f;
                introPitch -= md.y * 0.003f;
                if (introPitch >  1.4f) introPitch = 1.4f;
                if (introPitch < -1.4f) introPitch = -1.4f;

                float spd = 4.0f * dt;
                Vector3 fwd = { cosf(introYaw), 0, sinf(introYaw) };
                Vector3 rgt = { -sinf(introYaw), 0, cosf(introYaw) };
                if (IsKeyDown(KEY_W)) { introPos.x += fwd.x*spd; introPos.z += fwd.z*spd; }
                if (IsKeyDown(KEY_S)) { introPos.x -= fwd.x*spd; introPos.z -= fwd.z*spd; }
                if (IsKeyDown(KEY_D)) { introPos.x += rgt.x*spd; introPos.z += rgt.z*spd; }
                if (IsKeyDown(KEY_A)) { introPos.x -= rgt.x*spd; introPos.z -= rgt.z*spd; }
                introPos.y = 1.6f;  // locked to eye height
                if (introPos.x >  7.4f) introPos.x =  7.4f; if (introPos.x < -7.4f) introPos.x = -7.4f;
                if (introPos.z >  35.5f) introPos.z =  35.5f; if (introPos.z < -1.0f) introPos.z = -1.0f;
            } else {
                // ---- EDITOR god-mode: free-fly, no bounds, vertical move ----
                // Hold RIGHT mouse to look (keeps LEFT click free for selecting).
                // Only lock the cursor on the frame RMB is pressed, free it on
                // release — calling Disable/Enable every frame re-centers it
                // (that's the "stuck in the middle" jitter).
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) DisableCursor();
                if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) EnableCursor();
                if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                    Vector2 md = GetMouseDelta();
                    introYaw += md.x * 0.003f;
                    introPitch -= md.y * 0.003f;
                    if (introPitch >  1.5f) introPitch = 1.5f;
                    if (introPitch < -1.5f) introPitch = -1.5f;
                }
                // fly faster with Shift
                float spd = (IsKeyDown(KEY_LEFT_SHIFT) ? 16.0f : 7.0f) * dt;
                Vector3 fdir = { cosf(introPitch)*cosf(introYaw), sinf(introPitch), cosf(introPitch)*sinf(introYaw) };
                Vector3 rgt  = { -sinf(introYaw), 0, cosf(introYaw) };
                if (IsKeyDown(KEY_W)) { introPos.x += fdir.x*spd; introPos.y += fdir.y*spd; introPos.z += fdir.z*spd; }
                if (IsKeyDown(KEY_S)) { introPos.x -= fdir.x*spd; introPos.y -= fdir.y*spd; introPos.z -= fdir.z*spd; }
                if (IsKeyDown(KEY_D)) { introPos.x += rgt.x*spd; introPos.z += rgt.z*spd; }
                if (IsKeyDown(KEY_A)) { introPos.x -= rgt.x*spd; introPos.z -= rgt.z*spd; }
                if (IsKeyDown(KEY_SPACE))      introPos.y += spd;   // up
                if (IsKeyDown(KEY_LEFT_CONTROL)) introPos.y -= spd; // down
                // no bounds in god-mode (fly anywhere)
            }

            // In edit mode the door trigger is OFF so you can build freely.
            bool atDoor = (!editMode && introPos.z < -0.2f && fabsf(introPos.x) < 1.2f);
            if (atDoor) { EnableCursor(); flashStart = GetTime(); screen = SCREEN_FLASH; }

            Vector3 idir = { cosf(introPitch)*cosf(introYaw), sinf(introPitch), cosf(introPitch)*sinf(introYaw) };
            Camera3D icam = { 0 };
            icam.position = introPos;
            icam.target = (Vector3){ introPos.x+idir.x, introPos.y+idir.y, introPos.z+idir.z };
            icam.up = (Vector3){0,1,0}; icam.fovy = 75; icam.projection = CAMERA_PERSPECTIVE;

            // ---- LEVEL EDITOR ----
            // Where the camera ray hits the floor (y=0) — the placement point.
            Vector3 aim = { introPos.x, 0, introPos.z };
            if (idir.y < -0.001f) {
                float dist = -introPos.y / idir.y;       // ray to y=0 plane
                if (dist > 0 && dist < 80) {
                    aim = (Vector3){ introPos.x + idir.x*dist, 0, introPos.z + idir.z*dist };
                    if (gridSnap) { aim.x = roundf(aim.x*2)/2; aim.z = roundf(aim.z*2)/2; }
                }
            }
            if (editMode) {
                // palette: 1-6 pick prop type, C cycles color, G toggles grid snap
                if (IsKeyPressed(KEY_ONE))   paletteType = 0;
                if (IsKeyPressed(KEY_TWO))   paletteType = 1;
                if (IsKeyPressed(KEY_THREE)) paletteType = 2;
                if (IsKeyPressed(KEY_FOUR))  paletteType = 3;
                if (IsKeyPressed(KEY_FIVE))  paletteType = 4;
                if (IsKeyPressed(KEY_SIX))   paletteType = 5;
                if (IsKeyPressed(KEY_C))     paletteColor = (paletteColor+1)%8;
                if (IsKeyPressed(KEY_G))     gridSnap = !gridSnap;

                // LEFT click on empty floor (not right-dragging to look) = place;
                // but if click hits an existing prop, select it instead.
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                    // pick: nearest prop along the view ray
                    float best=9e9f; int bi=-1;
                    for (int i=0;i<g_propCount;i++){
                        Vector3 to={g_props[i].pos.x-introPos.x,g_props[i].pos.y-introPos.y,g_props[i].pos.z-introPos.z};
                        float len=sqrtf(to.x*to.x+to.y*to.y+to.z*to.z); if(len<0.01f)continue;
                        Vector3 tn={to.x/len,to.y/len,to.z/len};
                        float dot=tn.x*idir.x+tn.y*idir.y+tn.z*idir.z;
                        if(dot>0.985f && len<best){best=len;bi=i;}
                    }
                    if (bi>=0) propSel=bi;            // clicked a prop -> select
                    else if (g_propCount<MAX_PROPS) { // empty -> place new
                        Vector3 sz = DefaultSize((PropType)paletteType);
                        Prop np; np.type=(PropType)paletteType; np.size=sz; np.rotY=0;
                        np.color=PROP_PALETTE[paletteColor];
                        np.texId = texSel;   // assign currently-chosen texture (or -1)
                        np.pos=(Vector3){aim.x, sz.y/2, aim.z};   // sit on floor
                        g_props[g_propCount]=np; propSel=g_propCount; g_propCount++;
                    }
                }
                // edit the selected prop
                if (propSel>=0 && propSel<g_propCount) {
                    Prop *p=&g_props[propSel];
                    float ms=5.0f*dt;
                    if (IsKeyDown(KEY_RIGHT)) p->pos.x+=ms;
                    if (IsKeyDown(KEY_LEFT))  p->pos.x-=ms;
                    if (IsKeyDown(KEY_UP))    p->pos.z-=ms;
                    if (IsKeyDown(KEY_DOWN))  p->pos.z+=ms;
                    if (IsKeyDown(KEY_PAGE_UP))   p->pos.y+=ms;
                    if (IsKeyDown(KEY_PAGE_DOWN)) p->pos.y-=ms;
                    // scale all dims with [ ]
                    if (IsKeyDown(KEY_RIGHT_BRACKET)){ p->size.x+=ms; p->size.y+=ms; p->size.z+=ms; }
                    if (IsKeyDown(KEY_LEFT_BRACKET)){ p->size.x-=ms; p->size.y-=ms; p->size.z-=ms;
                        if(p->size.x<0.1f)p->size.x=0.1f; if(p->size.y<0.1f)p->size.y=0.1f; if(p->size.z<0.1f)p->size.z=0.1f; }
                    // rotate with , .
                    if (IsKeyDown(KEY_COMMA))  p->rotY-=60*dt;
                    if (IsKeyDown(KEY_PERIOD)) p->rotY+=60*dt;
                    // recolor selected with C
                    if (IsKeyPressed(KEY_C)) p->color=PROP_PALETTE[paletteColor];
                    // T cycles texture on the selected prop: -1(none)->0->1->...->none
                    if (IsKeyPressed(KEY_T)) {
                        p->texId = (p->texId+1 >= g_texCount) ? -1 : p->texId+1;
                        texSel = p->texId;
                    }
                    // Delete / Duplicate
                    if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_X)) {
                        for(int i=propSel;i<g_propCount-1;i++) g_props[i]=g_props[i+1];
                        g_propCount--; propSel=-1;
                    }
                    if (IsKeyPressed(KEY_TAB) && g_propCount<MAX_PROPS) {  // duplicate
                        g_props[g_propCount]=*p; g_props[g_propCount].pos.x+=1.0f;
                        propSel=g_propCount; g_propCount++;
                    }
                }
                // Ctrl+S save map
                if ((IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_S))
                    SaveMap();
            }

            BeginDrawing();
                // IMPORTANT: ClearBackground clears the depth buffer too. Without
                // it the 3D depth test accumulates garbage and the scene slowly
                // corrupts/whites out. Clear first, THEN draw the gradient on top.
                ClearBackground((Color){236,234,228,255});
                DrawRectangleGradientV(0,0,W,H, (Color){248,247,244,255}, (Color){224,222,216,255});
                BeginMode3D(icam);
                    // ---- MUSEUM gallery: textured if pngs present, else marble ----
                    if (hasFloor)
                        DrawTexturedQuad(texFloor,(Vector3){0,0,17},(Vector3){8,0,0},(Vector3){0,0,19}, 8.0f);
                    else {
                        DrawPlane((Vector3){0,0,17},(Vector2){16,38},(Color){222,220,214,255});
                        for (int gz=-1; gz<=36; gz+=2)
                            DrawLine3D((Vector3){-8,0.01f,(float)gz},(Vector3){8,0.01f,(float)gz},(Color){210,208,202,255});
                        for (int gx=-8; gx<=8; gx+=2)
                            DrawLine3D((Vector3){(float)gx,0.01f,-1},(Vector3){(float)gx,0.01f,36},(Color){210,208,202,255});
                    }
                    DrawCube((Vector3){0,5,17},16,0.2f,38,(Color){248,246,242,255});         // ceiling
                    // walls: textured quads if wall.png, else solid cubes
                    if (hasWall) {
                        DrawTexturedQuad(texWall,(Vector3){-7.9f,2.5f,17},(Vector3){0,0,19},(Vector3){0,2.5f,0}, 6.0f); // left
                        DrawTexturedQuad(texWall,(Vector3){7.9f,2.5f,17}, (Vector3){0,0,19},(Vector3){0,2.5f,0}, 6.0f); // right
                        DrawTexturedQuad(texWall,(Vector3){0,2.5f,36.1f},(Vector3){8,0,0},(Vector3){0,2.5f,0}, 4.0f);  // far
                    } else {
                        DrawCube((Vector3){-8,2.5f,17},0.3f,5,38,(Color){244,242,236,255});
                        DrawCube((Vector3){8,2.5f,17},0.3f,5,38,(Color){244,242,236,255});
                        DrawCube((Vector3){0,2.5f,36.2f},16,5,0.3f,(Color){240,238,232,255});
                    }
                    // entrance wall with door gap (exit to the program)
                    DrawCube((Vector3){-4,2.5f,-1},8,5,0.3f,(Color){238,236,230,255});
                    DrawCube((Vector3){4,2.5f,-1},8,5,0.3f,(Color){238,236,230,255});
                    DrawCube((Vector3){0,4.2f,-1},2.4f,1.4f,0.3f,(Color){238,236,230,255});   // lintel
                    DrawCube((Vector3){0,1.4f,-1.05f},2.2f,2.8f,0.05f,(Color){40,45,55,255}); // doorway

                    // ---- exhibits: pedestals down both sides with display objects ----
                    for (int zz = 6; zz <= 30; zz += 8) {
                        // left pedestal + exhibit
                        DrawCube((Vector3){-5.5f,0.6f,(float)zz},1.4f,1.2f,1.4f,(Color){210,208,202,255});
                        DrawCubeWires((Vector3){-5.5f,0.6f,(float)zz},1.4f,1.2f,1.4f,(Color){180,178,172,255});
                        DrawCube((Vector3){-5.5f,1.6f,(float)zz},0.7f,0.7f,0.7f,(Color){120,150,190,255});
                        // right pedestal + exhibit
                        DrawCube((Vector3){5.5f,0.6f,(float)zz},1.4f,1.2f,1.4f,(Color){210,208,202,255});
                        DrawCubeWires((Vector3){5.5f,0.6f,(float)zz},1.4f,1.2f,1.4f,(Color){180,178,172,255});
                        DrawSphere((Vector3){5.5f,1.7f,(float)zz},0.5f,(Color){190,150,110,255});
                        // framed "paintings" on the walls (textured if art pngs present)
                        DrawCube((Vector3){-7.8f,2.6f,(float)zz},0.1f,1.7f,2.3f,(Color){70,55,40,255}); // L frame
                        if (hasArt1) DrawTexturedQuad(texArt1,(Vector3){-7.73f,2.6f,(float)zz},(Vector3){0,0,0.95f},(Vector3){0,0.65f,0},1.0f);
                        else DrawCube((Vector3){-7.74f,2.6f,(float)zz},0.05f,1.3f,1.9f,(Color){170,185,205,255});
                        DrawCube((Vector3){7.8f,2.6f,(float)zz},0.1f,1.7f,2.3f,(Color){70,55,40,255});  // R frame
                        if (hasArt2) DrawTexturedQuad(texArt2,(Vector3){7.73f,2.6f,(float)zz},(Vector3){0,0,0.95f},(Vector3){0,0.65f,0},1.0f);
                        else DrawCube((Vector3){7.74f,2.6f,(float)zz},0.05f,1.3f,1.9f,(Color){200,180,160,255});
                    }

                    // ---- user-loaded 3D models (.obj/.glb) ----
                    for (int i=0;i<modelCount;i++)
                        DrawModel(loadedModels[i], modelPos[i], modelScale[i], WHITE);

                    // ---- placed editor props ----
                    for (int i=0;i<g_propCount;i++) {
                        DrawProp(&g_props[i], WHITE, false);
                        if (editMode && i==propSel) {
                            float pulse = 0.5f + 0.5f*sinf(t*6.0f);
                            Vector3 s = g_props[i].size;
                            float mx = (s.x>s.y?s.x:s.y); mx = (mx>s.z?mx:s.z);
                            DrawCubeWires(g_props[i].pos, s.x*1.1f, s.y*1.1f, s.z*1.1f,
                                          (Color){255,150,40,(unsigned char)(150+pulse*100)});
                            DrawCircle3D((Vector3){g_props[i].pos.x,0.02f,g_props[i].pos.z}, mx*0.7f,
                                         (Vector3){1,0,0},90.0f,(Color){255,160,40,200});
                        }
                    }

                    // ---- editor: grid + placement ghost ----
                    if (editMode) {
                        DrawGrid(40, 1.0f);
                        Vector3 gs = DefaultSize((PropType)paletteType);
                        Color ghost = PROP_PALETTE[paletteColor]; ghost.a = 120;
                        DrawCubeWires((Vector3){aim.x, gs.y/2, aim.z}, gs.x,gs.y,gs.z, (Color){60,120,200,255});
                        DrawCube((Vector3){aim.x, gs.y/2, aim.z}, gs.x,gs.y,gs.z, ghost);
                    }
                EndMode3D();

                if (!editMode) {
                    const char *pr = "Welcome to the museum  -  walk to the exit  (WASD + mouse)   -   E: editor";
                    Txt(pr, W/2 - TxtW(pr,20)/2, H-60, 20, (Color){70,72,84,255});
                } else {
                    // ===== EDITOR UI =====
                    // top bar
                    DrawRectangle(0,0,W,28,(Color){26,28,36,235});
                    Txt("LEVEL EDITOR", 12, 6, 18, (Color){235,238,245,255});
                    Txt("RMB look | WASD+Spc/Ctrl fly | Click place/select | Arrows move | [ ] scale | , . rotate | T texture | C color | X del | Tab dupe | Ctrl+S save | E exit",
                        150, 8, 12, (Color){150,155,170,255});

                    // left palette panel
                    int px=10, pyy=40, pw=150;
                    DrawRectangle(px-4, pyy-4, pw+8, 8+PROP_TYPE_COUNT*26+70, (Color){26,28,36,230});
                    Txt("PROPS (1-6)", px+6, pyy, 14, (Color){200,205,215,255});
                    for (int i=0;i<PROP_TYPE_COUNT;i++) {
                        Rectangle rb={px,(float)(pyy+22+i*26),pw,22};
                        bool selp = (paletteType==i);
                        DrawRectangle((int)rb.x,(int)rb.y,(int)rb.width,(int)rb.height, selp?(Color){70,110,180,255}:(Color){40,42,52,255});
                        Txt(TextFormat("%d  %s", i+1, PROP_NAMES[i]), px+8, pyy+25+i*26, 14, RAYWHITE);
                    }
                    // color swatch
                    int cy = pyy+22+PROP_TYPE_COUNT*26+8;
                    Txt("COLOR (C)", px+6, cy, 13, (Color){200,205,215,255});
                    for (int i=0;i<8;i++)
                        DrawRectangle(px+(i%8)*17, cy+18, 15, 15, (i==paletteColor)?(Color){255,255,255,255}:PROP_PALETTE[i]);
                    for (int i=0;i<8;i++)
                        DrawRectangle(px+(i%8)*17+1, cy+19, 13, 13, PROP_PALETTE[i]);
                    DrawText(gridSnap?"Grid: ON (G)":"Grid: OFF (G)", px+6, cy+40, 12, gridSnap?(Color){140,200,150,255}:(Color){200,150,150,255});

                    // ---- TEXTURE panel (right side): imported images ----
                    int tx=W-180, ty=40, tw2=170;
                    DrawRectangle(tx-4,ty-4,tw2+8, 8+24+(g_texCount>0?g_texCount:1)*40, (Color){26,28,36,230});
                    Txt(TextFormat("TEXTURES (%d)  T:apply", g_texCount), tx+6, ty, 13, (Color){200,205,215,255});
                    if (g_texCount==0)
                        DrawText("drop .png in /textures", tx+6, ty+26, 11, (Color){170,170,180,255});
                    for (int i=0;i<g_texCount;i++) {
                        int ry=ty+24+i*40;
                        bool selt = (propSel>=0 && g_props[propSel].texId==i);
                        if (selt) DrawRectangle(tx-2,ry-2,tw2+4,38,(Color){70,110,180,255});
                        DrawTexturePro(g_texLib[i], (Rectangle){0,0,(float)g_texLib[i].width,(float)g_texLib[i].height},
                                       (Rectangle){(float)tx,(float)ry,34,34}, (Vector2){0,0}, 0, WHITE);
                        Txt(g_texName[i], tx+40, ry+8, 12, RAYWHITE);
                    }

                    // bottom status
                    if (propSel>=0 && propSel<g_propCount) {
                        Prop*p=&g_props[propSel];
                        Txt(TextFormat("Selected: %s   pos %.1f,%.1f,%.1f   size %.1f,%.1f,%.1f   rot %.0f   (%d props)",
                            PROP_NAMES[p->type], p->pos.x,p->pos.y,p->pos.z, p->size.x,p->size.y,p->size.z, p->rotY, g_propCount),
                            12, H-26, 15, (Color){255,170,60,255});
                    } else {
                        Txt(TextFormat("Click floor to place a %s   (%d props placed)", PROP_NAMES[paletteType], g_propCount),
                            12, H-26, 15, (Color){150,155,170,255});
                    }
                }
            EndDrawing();
        }
        else if (screen == SCREEN_FLASH)
        {
            // ---- "Program starting..." flash, ~1.6s, then to login ----
            double el = GetTime() - flashStart;
            if (el > 1.6) { screen = SCREEN_USERNAME; }
            // fade: white flash in, then text
            BeginDrawing();
                ClearBackground(BLACK);
                float a = (el < 0.25) ? (el/0.25f) : 1.0f;      // quick fade-in
                const char *t1 = "PROGRAM STARTING";
                Txt(t1, W/2 - TxtW(t1,36)/2, H/2-30, 36, (Color){230,230,240,(unsigned char)(a*255)});
                // dots
                int dots = ((int)(el*3)) % 4;
                char d[8] = ""; for (int i=0;i<dots;i++) d[i]='.'; d[dots]='\0';
                Txt(d, W/2 - TxtW("...",36)/2, H/2+20, 36, (Color){150,160,200,(unsigned char)(a*255)});
            EndDrawing();
        }
        else if (screen == SCREEN_USERNAME)
        {
            // Tab / click switches which field has focus.
            if (IsKeyPressed(KEY_TAB)) loginField ^= 1;

            // Type into the focused field.
            char *target = (loginField == 0) ? g_username : loginPw;
            size_t cap   = (loginField == 0) ? sizeof(g_username) : sizeof(loginPw);
            int c = GetCharPressed();
            while (c > 0) {
                size_t len = strlen(target);
                // Username (field 0) is restricted to [A-Za-z0-9_-] so it is safe
                // to embed in URLs/commands and matches the server's validation.
                // Password (field 1) allows any printable ASCII.
                bool ok = (loginField == 0)
                    ? ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')
                    : (c >= 32 && c < 127);
                if (ok && len < cap - 1) { target[len] = (char)c; target[len+1] = '\0'; }
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
                DrawRectangleRounded(uBox, 0.05f, 4, (Color){26,27,33,255});
                DrawRectangleRoundedLines(uBox, 0.05f, 4,
                    loginField==0 ? (Color){110,115,130,255} : (Color){56,58,68,255});
                char ub[40]; snprintf(ub, sizeof(ub), "%s%s", g_username,
                    (loginField==0 && ((int)(GetTime()*2))%2) ? "_" : "");
                Txt(g_username[0]?ub:"username", (int)uBox.x+14, (int)uBox.y+9, 19,
                    g_username[0]?(Color){220,222,230,255}:(Color){95,98,110,255});

                // password box (masked)
                DrawRectangleRounded(pBox, 0.05f, 4, (Color){26,27,33,255});
                DrawRectangleRoundedLines(pBox, 0.05f, 4,
                    loginField==1 ? (Color){110,115,130,255} : (Color){56,58,68,255});
                char stars[64]; size_t pl = strlen(loginPw);
                for (size_t i=0;i<pl && i<sizeof(stars)-2;i++) stars[i]='*';
                stars[pl<sizeof(stars)-2?pl:sizeof(stars)-2]='\0';
                if (loginField==1 && ((int)(GetTime()*2))%2) strncat(stars,"_",2);
                Txt(loginPw[0]?stars:"password", (int)pBox.x+14, (int)pBox.y+9, 19,
                    loginPw[0]?(Color){220,222,230,255}:(Color){95,98,110,255});

                // submit button (flat, sharp, accent blue)
                Color gbg = !canGo ? (Color){34,35,42,255} : ACC_BLUE;
                DrawRectangleRounded(go, 0.04f, 4, gbg);
                DrawRectangleRoundedLines(go, 0.04f, 4, (Color){70,90,125,160});
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

                // Update button (bottom-right) + version + status — works before login
                Rectangle luBtn = { (float)(W-150), (float)(H-50), 130, 32 };
                if (Button(luBtn, "Check for Update", 13, (Color){26,27,33,255}, (Color){38,40,48,255}, (Color){180,183,193,255}, m)) {
                    strcpy(g_updateMsg, "Checking for updates..."); CheckForUpdate();
                }
                Txt(APP_VERSION, (int)luBtn.x, (int)(luBtn.y-20), 13, (Color){110,112,122,255});
                if (g_updateMsg[0]) {
                    int mw = TxtW(g_updateMsg, 13);
                    Txt(g_updateMsg, W-20-mw, H-72, 13, (Color){150,153,163,255});
                }
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
                        resMode = (resMode + 1) % resCount;
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
                            // green if that friend is currently online (exact match)
                            int online = NameOnline(fl);
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

            // Refresh match state (scores/round/winner) ~2x/sec. Without this the
            // scoreboard and the win banner never update during a match.
            if (GetTime() >= nextGamePoll) { GamePoll(); nextGamePoll = GetTime() + 0.5; }

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
                char un[128]; UrlEncode(g_username, un, sizeof(un));
                snprintf(cmd,sizeof(cmd),"curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" \"%s/score?name=%s\"", g_serverURL, un);
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
                        snprintf(cmd,sizeof(cmd),"curl -s -m 4 -H \"ngrok-skip-browser-warning: 1\" \"%s/resetmatch\"", g_serverURL);
                        RunCapture(cmd,out,sizeof(out));
                        inMatchFPS=false; screen=SCREEN_LOBBY;
                    }
                }

                Txt("WASD move  -  Mouse look  -  Click shoot  -  Esc leave",
                    24, H-72, 13, (Color){120,120,130,255});
            EndDrawing();
        }
    }

    for (int i=0;i<modelCount;i++) UnloadModel(loadedModels[i]);
    for (int i=0;i<g_texCount;i++) UnloadTexture(g_texLib[i]);
    if (hasFloor) UnloadTexture(texFloor);
    if (hasWall)  UnloadTexture(texWall);
    if (hasCeil)  UnloadTexture(texCeil);
    if (hasArt1)  UnloadTexture(texArt1);
    if (hasArt2)  UnloadTexture(texArt2);
    voice_shutdown();
    game_shutdown();
    if (g_fontOK) UnloadFont(g_font);
    CloseWindow();
    return 0;
}
