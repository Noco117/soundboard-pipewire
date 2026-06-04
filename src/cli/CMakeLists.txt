set(SOURCE_FILES 
    main.cpp
)

add_executable(soundboard-cli ${SOURCE_FILES})
set_target_properties(soundboard-cli PROPERTIES
    OUTPUT_NAME "soundboard"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_include_directories(soundboard-cli PRIVATE 
"${CMAKE_SOURCE_DIR}/include"
)

# --- SYSTEM INSTALLATION RULE ---
install(TARGETS soundboard-cli DESTINATION ${CMAKE_INSTALL_BINDIR})