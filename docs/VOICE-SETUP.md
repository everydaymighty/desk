# Push-to-talk voice — setup

Basic voice: hold **V** in the lobby to talk; you hear others who are talking.
16kHz mono, raw relay, no codec. Proof of concept, not Discord.

## One-time: get miniaudio.h (goes in client/)
```
cd /c/Users/illum/Desktop/Projects/desk/client
curl -L -o miniaudio.h https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
```

## Build (Windows, MINGW64) — compile BOTH .c files
```
cd /c/Users/illum/Desktop/Projects/desk/client
gcc main.c voice.c -o black.exe -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -lole32
```
(add `-static -lglfw3 -lpthread` for a standalone exe)

## Build (macOS)
```
cc main.c voice.c game.c -o black $(pkg-config --cflags --libs raylib) \
  -framework Cocoa -framework IOKit -framework OpenGL \
  -framework CoreAudio -framework AudioToolbox
```

## Run order
```
node ../servers/server.js         # lobby + chat + friends + game + accounts (8080)
node ../servers/voice-server.js   # voice relay (UDP 8081)
./black.exe
```
In the lobby, hold **V** — red "TALKING" dot. To hear it, run a second copy
(different account) on the same machine or same-LAN computer.

## IMPORTANT limits (firm)
- Voice uses UDP. Works on **localhost and same-LAN** only.
- It will **NOT** travel through ngrok's free tier (no UDP). Remote internet
  voice needs a real always-on server. Presence/chat DO work remotely via ngrok.
- `desk_voice.txt` holds the voice server address (default `127.0.0.1`). On a
  LAN, other computers put YOUR PC's local IP (e.g. `192.168.1.50`).
- No echo cancellation. Use headphones.
