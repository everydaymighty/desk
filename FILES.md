# What every file is (project map)

## The app (C source — compiled into black.exe)
- `main.c`      — the whole client: login, desktop, lobby, settings, 1v1 game UI
- `voice.c/.h`  — push-to-talk voice (mic/speaker/UDP), kept separate from raylib
- `game.c/.h`   — 1v1 game networking (runs on a background thread, talks HTTP)
- `miniaudio.h` — third-party audio library (downloaded, not edited)

## Build / run scripts
- `build-windows.bat` — compiles black.exe on Windows (MSYS2/MINGW64)
- `build-mac.sh`      — compiles on macOS
- `start-servers.bat` — launches the servers in their own windows

## Servers (Node.js — run on the HOST PC)
- `servers/server.js`       — THE server. Does login, presence, chat, friends,
                              leaderboard, AND game sync. This is the only one
                              you need to run for online play.
- `servers/voice-server.js` — voice relay (UDP 8081, LAN only)

## Config (must sit next to black.exe — read by filename at startup)
- `desk_server.txt` — server address (localhost, or your ngrok URL)
- `desk_auth.txt`   — UNUSED (login is on the lobby server; file is ignored)
- `desk_voice.txt`  — voice server address (127.0.0.1 = LAN)

## Built app (what actually runs)
- `black.exe`, `libraylib.dll`, `glfw3.dll`

## Sharing
- `share/` — the bundle you zip and send to a friend (exe + dlls + configs + README)

## Secrets (NEVER share / committed-blocked by .gitignore)
- `auth-secret.key`, `users.json`, `friends.json`, `leaderboard.json`

## Docs
- `README.md`, `docs/` (AUTH-SETUP, VOICE-SETUP, UPDATE-SETUP), `SERVER-SETUP.md`

## Auto-generated at runtime (safe to delete anytime; they come back)
- `desk_online.txt`, `desk_chat.txt`, `desk_friends.txt`, `desk_lb.txt`,
  `desk_game.txt`, `desk_gget.txt`, `desk_gdmg.txt`
