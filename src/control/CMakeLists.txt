# Typio GTK4 control panel

add_executable(typio-control
    main.c
    control_models.c
    control_ui.c
    control_bind.c
)

target_include_directories(typio-control PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_SOURCE_DIR}/src/server
    ${GTK4_INCLUDE_DIRS}
)

target_compile_options(typio-control PRIVATE ${GTK4_CFLAGS_OTHER})
target_link_libraries(typio-control PRIVATE typio-core ${GTK4_LIBRARIES})

set_target_properties(typio-control PROPERTIES
    OUTPUT_NAME "typio-control"
)

install(TARGETS typio-control
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
