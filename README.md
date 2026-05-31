# Black — cross-platform fullscreen black app

A blank fullscreen black window built with [raylib](https://www.raylib.com). Press **ESC** to quit. We add features in `main.c`.

## macOS
1. Install raylib once: `brew install raylib`
2. Build: `bash build-mac.sh`
3. Run: `./black`

## Windows
1. Install [MSYS2](https://www.msys2.org).
2. In the **MSYS2 MINGW64** terminal: `pacman -S mingw-w64-x86_64-raylib mingw-w64-x86_64-gcc`
3. Build: run `build-windows.bat` (from that terminal, or with MinGW gcc on PATH).
4. Run: `black.exe`

## Where to add things
In `main.c`:
- **Input / logic** → under `// ---- UPDATE`
- **Drawing** → under `// ---- DRAW YOUR STUFF HERE`
