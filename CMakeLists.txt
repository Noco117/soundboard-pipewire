cmake_minimum_required(VERSION 3.15)
project(MyProject VERSION 1.0 LANGUAGES CXX)

# Force C++20 or higher
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Export Compile Commands for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Require PkgConfig
find_package(PkgConfig REQUIRED)

add_subdirectory(src)
add_subdirectory(dist) # This only copies files from dist, currently systemd user service file for the daemon.

# --- SYSTEM INSTALLATION RULES ---
include(GNUInstallDirs)
install(PROGRAMS src/cli/soundboard-cli DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME soundboard)