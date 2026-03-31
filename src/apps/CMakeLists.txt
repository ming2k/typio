add_subdirectory(daemon)

if(ENABLE_STATUS_BUS)
    add_subdirectory(cli)
endif()

if(BUILD_CONTROL_PANEL)
    add_subdirectory(control)
endif()
