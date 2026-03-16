# Typio GTK4 control panel

add_executable(typio-control
    main.c
    control_config.c
    control_models.c
    control_widgets.c
    control_window.c
    control_ui.c
    control_bind.c
)

target_include_directories(typio-control PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src/lib
    ${GTK4_INCLUDE_DIRS}
)

target_compile_definitions(typio-control PRIVATE
    TYPIO_CONTROL_CSS_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/typio-control.css"
    TYPIO_CONTROL_CSS_INSTALL_PATH="${TYPIO_INSTALL_DATADIR}/typio-control.css"
)

target_compile_options(typio-control PRIVATE ${GTK4_CFLAGS_OTHER})
target_link_libraries(typio-control PRIVATE typio-core ${GTK4_LIBRARIES})

set_target_properties(typio-control PROPERTIES
    OUTPUT_NAME "typio-control"
)

install(TARGETS typio-control
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/typio-control.css
    DESTINATION ${TYPIO_INSTALL_DATADIR}
)
