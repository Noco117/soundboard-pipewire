set(SOURCE_FILES 
    main.cpp 
    soundboard.cpp
    virtualdevices.cpp
)

# 1. Find the PkgConfig tool
find_package(PkgConfig REQUIRED)

# 2. Use PkgConfig to find libpipewire-0.3
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)

add_executable(soundboard ${SOURCE_FILES})

target_include_directories(soundboard PRIVATE 
"${CMAKE_CURRENT_SOURCE_DIR}/../include"
)

target_link_libraries(soundboard PRIVATE PkgConfig::PULSE)