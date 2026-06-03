if(PKG_CONFIG_FOUND)
    pkg_check_modules(SYSTEMD QUIET systemd)
endif()

if (SYSTEMD_FOUND)
    message(STATUS "Systemd detected!")
    pkg_get_variable(SYSTEMD_USER_UNIT_DIR systemd systemduserunitdir)

    if(NOT SYSTEMD_USER_UNIT_DIR)
        message(STATUS "No systemd user unit directory found!")
        break()
    endif()

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/systemd/soundboard.service DESTINATION ${SYSTEMD_USER_UNIT_DIR})
else()
    message(STATUS "Systemd not found, skipping service file installation.")
endif()