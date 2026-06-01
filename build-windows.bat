@echo off
REM Build a STANDALONE black.exe (runs without MSYS2 installed).
REM Run this from the MSYS2 MINGW64 shell, OR double-click if gcc is on PATH.
REM
REM One-time setup in the MINGW64 terminal:
REM   pacman -S --needed --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-raylib mingw-w64-x86_64-glfw

gcc main.c voice.c -o black.exe -static ^
  -lraylib -lglfw3 -lopengl32 -lgdi32 -lwinmm -lws2_32 -lole32 -lpthread

echo Done. If black.exe was created, it runs standalone (shareable).
pause
