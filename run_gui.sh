#!/bin/bash
set -e
# Clean configure (hint CMake where SDL2 cmake files live on Homebrew)
rm -rf build
mkdir build && cd build
cmake .. -DSDL2_DIR="$(brew --prefix sdl2)/lib/cmake/SDL2"
cmake --build . --target cpu_gui
./cpu_gui