set -e
# Clean configure (hint CMake where SDL2 cmake files live on Homebrew)
rm -rf build
mkdir build && cd build
cmake ..
cmake --build .
./cpu_cli