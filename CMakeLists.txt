cmake_minimum_required(VERSION 3.15)
project(MyProject VERSION 1.0 LANGUAGES CXX)

# Force C++17 or higher
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export Compile Commands for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add the src subdirectory to the build graph
add_subdirectory(src)