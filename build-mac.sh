#!/bin/bash
# macOS build. First install raylib once:  brew install raylib
# Then run:  bash build-mac.sh   (creates ./black)

cc main.c -o black \
  $(pkg-config --cflags --libs raylib) \
  -framework Cocoa -framework IOKit -framework OpenGL

echo "Built ./black  — run it with: ./black"
