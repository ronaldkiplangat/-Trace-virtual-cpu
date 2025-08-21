cmake_minimum_required(VERSION 3.20)
project(cpu_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Core CPU library ----
add_library(cpu_core
    src/cpu.cpp
    src/demo_program.cpp
)
# Your headers live in include/
target_include_directories(cpu_core PUBLIC include)

# ---- CLI simulator (no GUI) ----
add_executable(cpu_cli
    src/main.cpp
)
target_link_libraries(cpu_cli PRIVATE cpu_core)
