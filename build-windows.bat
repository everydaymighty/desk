@echo off
REM Windows build with MinGW + raylib.
REM 1) Install MSYS2 (https://www.msys2.org)
REM 2) In the MSYS2 MINGW64 terminal run:  pacman -S mingw-w64-x86_64-raylib mingw-w64-x86_64-gcc
REM 3) Run this from that same MINGW64 terminal, or with MinGW's gcc on PATH.

gcc main.c -o black.exe -lraylib -lopengl32 -lgdi32 -lwinmm

echo Built black.exe  — run it by double-clicking or: black.exe
pause
