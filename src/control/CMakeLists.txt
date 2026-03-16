# Typio GTK4 control panel

find_program(GLIB_COMPILE_RESOURCES NAMES glib-compile-resources REQUIRED)

set(TYPIO_CONTROL_RESOURCES_C "${CMAKE_CURRENT_BINARY_DIR}/typio-control-resources.c")

add_custom_command(
    OUTPUT "${TYPIO_CONTROL_RESOURCES_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
            --target=typio-control-resources.c
            --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}
            --generate-source
            ${CMAKE_CURRENT_SOURCE_DIR}/typio-control.gresource.xml
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/typio-control.gresource.xml"
        "${CMAKE_CURRENT_SOURCE_DIR}/typio-control.css"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM
)

add_executable(typio-control
    main.c
    control_config.c
    control_models.c
    control_widgets.c
    control_window.c
    control_ui.c
    control_bind.c
    "${TYPIO_CONTROL_RESOURCES_C}"
)

target_include_directories(typio-control PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_CURRENT_BINARY_DIR}
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
