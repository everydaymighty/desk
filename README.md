# desk

A cross-platform desktop social app + game, written in C with [raylib](https://www.raylib.com).

You start in a **museum** you walk through, step through the door into a
**login** screen, then a **desktop** with a profile menu and settings. From
there you enter a **lobby** (3D room with everyone's cube, live chat, friends
list, push-to-talk voice, leaderboard) and can launch a **first-person 1v1
shooter**. There's also a built-in **level editor** for placing props and
applying textures.

## Folder layout
```
desk/
├─ main.c                 the whole client (intro, login, desktop, lobby, game, editor)
├─ voice.c / voice.h      push-to-talk voice (mic/speaker/UDP)
├─ game.c / game.h        1v1 game networking (background thread, HTTP sync)
├─ miniaudio.h            audio library (downloaded once, see docs/VOICE-SETUP.md)
├─ build-windows.bat      Windows build script
├─ build-mac.sh           macOS build script
├─ start-servers.bat      launches the servers in their own windows
├─ desk_server.txt        server address (login, presence, chat, friends, game)
├─ desk_voice.txt         voice server address (127.0.0.1 = LAN only)
├─ map.txt                saved editor map (props: type, position, texture...)
├─ models.txt             optional .obj/.glb models to load into the intro
├─ textures/              drop .png/.jpg here to import them into the editor
├─ servers/
│   ├─ server.js              login + presence + chat + friends + leaderboard + game  (port 8080)
│   └─ voice-server.js        voice relay                                        (UDP port 8081)
├─ share/                 the zip-and-send bundle for friends
└─ docs/                  AUTH-SETUP, VOICE-SETUP, UPDATE-SETUP, plus SERVER-SETUP.md
```
The config `.txt` files, `black.exe`, and the DLLs MUST stay together — the app
reads config from the folder it runs in.

## Build

**Windows** (in the MSYS2 MINGW64 terminal):
```
cd /c/Users/illum/Desktop/Projects/desk
gcc main.c voice.c game.c -o black.exe -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -lole32 -lpthread
```
One-time setup: install MSYS2, then
`pacman -S --needed --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-raylib mingw-w64-x86_64-glfw`

**macOS**:
```
cc main.c voice.c game.c -o black $(pkg-config --cflags --libs raylib) \
  -framework Cocoa -framework IOKit -framework OpenGL \
  -framework CoreAudio -framework AudioToolbox
```

## Run (host machine)
```
node servers/server.js        # everything except voice (port 8080)
node servers/voice-server.js  # voice relay (port 8081, LAN only)
./black.exe
```
For friends over the internet, expose the server with one ngrok tunnel:
```
ngrok http 8080
```
Put the https URL it prints into `desk_server.txt` (auth is merged into the main
server, so there's nothing else to set), then share the `share/` folder. See
`docs/SERVER-SETUP.md`.

## Controls
**Intro/museum:** WASD + mouse to walk · **E** = level editor · walk into the door to continue
**Editor:** RMB look · WASD+Space/Ctrl fly · click place/select · arrows move · `[ ]` scale · `, .` rotate · **T** texture · **C** color · **X** delete · **Tab** duplicate · **Ctrl+S** save
**Lobby:** **T** chat · **V** push-to-talk · click a cube for a profile · **Esc** back
**1v1:** WASD move · mouse look · click shoot · **Esc** leave
**Anywhere:** **F11** fullscreen

## Notes & limits
- One server, one ngrok tunnel handles login/chat/friends/leaderboard/game.
- Voice and the 1v1 game sync best on LAN; over free ngrok they work but lag.
- The host PC + server (+ ngrok) must stay running for friends to connect.
- Textures: drop images in `textures/`, then in the editor select a prop and
  press **T** to apply. Maps save to `map.txt` and reload on startup.
```
```
