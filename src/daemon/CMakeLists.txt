set(SOURCE_FILES 
    main.cpp 
    soundboard.cpp
    virtualdevices.cpp
    socketserver.cpp
)

# Use PkgConfig to find libpipewire-0.3
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)

add_executable(soundboard-daemon ${SOURCE_FILES})

set_target_properties(soundboard-daemon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(soundboard-daemon PRIVATE 
"${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(soundboard-daemon PRIVATE PkgConfig::PULSE)

# --- SYSTEM INSTALLATION RULE ---
install(TARGETS soundboard-daemon DESTINATION ${CMAKE_INSTALL_BINDIR})