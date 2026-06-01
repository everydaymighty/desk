# desk

A cross-platform desktop app (raylib/C) with a lobby: login, presence, floating
chat, push-to-talk voice, animated cubes, and an in-app updater.

## Folder layout
```
desk/
├─ main.c, voice.c, voice.h     the app (compiled together)
├─ miniaudio.h                  audio library (downloaded; see docs/VOICE-SETUP.md)
├─ build-windows.bat            Windows build script
├─ build-mac.sh                 macOS build script
├─ desk_server.txt              lobby server address (presence + chat)
├─ desk_auth.txt                auth server address (login)
├─ desk_voice.txt               voice server address
├─ servers/                     the Node services you run on the host PC
│   ├─ auth-server.js               accounts/login        (port 8090)
│   ├─ server.js                    presence + chat       (port 8080)
│   └─ voice-server.js              voice relay      (UDP  port 8081)
└─ docs/                        setup guides
    ├─ AUTH-SETUP.md
    ├─ VOICE-SETUP.md
    └─ UPDATE-SETUP.md
```

The config `.txt` files and `black.exe` MUST stay together — the app reads them
from the folder it runs in.

## Build (Windows, MINGW64)
```
cd /c/Users/illum/Desktop/Projects/desk
gcc main.c voice.c -o black.exe -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -lole32
```
For a standalone exe you can share, add: `-static -lglfw3 -lpthread`

## Run everything (local test)
```
node servers/auth-server.js
node servers/server.js
node servers/voice-server.js
./black.exe
```

## Controls
- Log in / create account on the first screen
- **Enter Lobby** (bottom-right)
- **T** chat · **V** push-to-talk · **F11** fullscreen · **Esc** back/quit

See `docs/` for auth, voice, and update details.
