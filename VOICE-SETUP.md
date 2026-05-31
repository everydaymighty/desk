# Push-to-talk voice — setup

Basic voice: hold **V** in the lobby to talk; you hear others who are talking.
16kHz mono, raw relay, no codec. This is a proof of concept, not Discord.

## One-time: get miniaudio.h
The app uses a single-file audio library. Download it into this folder:

1. Go to https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
2. Save it as `miniaudio.h` in `C:\Users\illum\Desktop\Projects\desk`
   (In the MINGW64 terminal you can also run:
   `curl -L -o miniaudio.h https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h` )

## Build (Windows, MINGW64) — compile BOTH .c files
Voice lives in voice.c (kept separate so miniaudio's windows.h doesn't clash
with raylib). Compile main.c AND voice.c together:
```
cd /c/Users/illum/Desktop/Projects/desk
gcc main.c voice.c -o black.exe -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -lole32
```

## Build (macOS)
```
cc main.c voice.c -o black $(pkg-config --cflags --libs raylib) \
  -framework Cocoa -framework IOKit -framework OpenGL \
  -framework CoreAudio -framework AudioToolbox
```

## Run it (all on YOUR PC)
Open these windows:
```
node server.js          # presence (port 8080)
node voice-server.js    # voice relay (UDP 8081)
./black.exe             # the app
```
Enter the lobby, hold **V** — you'll see a red "TALKING" dot. To actually hear
something, run a SECOND copy of the app (different username) on the same machine
or another computer on the SAME network, and talk from one.

## IMPORTANT limits (firm)
- Voice uses UDP. It works on **localhost and same-LAN** computers.
- It will **NOT** travel through ngrok's free tier (no UDP support). The 9 remote
  computers over the internet cannot do voice with this free setup — that needs a
  real always-on server (e.g. Oracle free VPS) later. Presence/lobby still work
  remotely via ngrok; only voice is LAN-limited for now.
- `desk_voice.txt` holds the voice server address (default `127.0.0.1`). On a LAN,
  other computers put YOUR PC's local IP (e.g. `192.168.1.50`) in their copy.
- No noise suppression / echo cancellation. Use headphones to avoid feedback.
