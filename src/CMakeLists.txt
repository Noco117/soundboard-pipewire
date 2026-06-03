set(SOURCE_FILES 
    daemon/main.cpp 
    daemon/soundboard.cpp
    daemon/virtualdevices.cpp
    daemon/socketserver.cpp
)

# 1. Find the PkgConfig tool
find_package(PkgConfig REQUIRED)

# 2. Use PkgConfig to find libpipewire-0.3
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)

add_executable(soundboard-daemon ${SOURCE_FILES})

set_target_properties(soundboard-daemon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(soundboard-daemon PRIVATE 
"${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(soundboard-daemon PRIVATE PkgConfig::PULSE)

add_custom_command(
    TARGET soundboard-daemon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/cli/soundboard-cli"
            "${CMAKE_BINARY_DIR}/bin/soundboard"
    COMMENT "Copying frontend Python CLI script to local build output folder..."
)

# --- SYSTEM INSTALLATION RULE ---
install(TARGETS soundboard-daemon DESTINATION ${CMAKE_INSTALL_BINDIR})