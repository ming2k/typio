add_library(test_lazy_engine_plugin MODULE test_lazy_engine_plugin.c)
target_link_libraries(test_lazy_engine_plugin PRIVATE typio-core)
set_target_properties(test_lazy_engine_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "test_lazy_engine_plugin"
)

add_executable(test_core test_core.c)
target_link_libraries(test_core PRIVATE typio-core)
add_test(NAME test_core COMMAND test_core)

add_executable(test_config test_config.c)
target_link_libraries(test_config PRIVATE typio-core)
add_test(NAME test_config COMMAND test_config)

add_executable(test_config_schema test_config_schema.c)
target_link_libraries(test_config_schema PRIVATE typio-core)
add_test(NAME test_config_schema COMMAND test_config_schema)

add_executable(test_rime_schema_list test_rime_schema_list.c)
target_link_libraries(test_rime_schema_list PRIVATE typio-core)
add_test(NAME test_rime_schema_list COMMAND test_rime_schema_list)

add_executable(test_engine_manager test_engine_manager.c)
target_link_libraries(test_engine_manager PRIVATE typio-core)
target_compile_definitions(test_engine_manager PRIVATE
    TYPIO_TEST_LAZY_ENGINE_PATH="$<TARGET_FILE:test_lazy_engine_plugin>"
)
add_dependencies(test_engine_manager test_lazy_engine_plugin)
add_test(NAME test_engine_manager COMMAND test_engine_manager)

add_executable(test_preedit_format
    test_inline_ui.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/preedit_format.c
)
target_include_directories(test_preedit_format PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_SOURCE_DIR}/src/server/wayland
)
add_test(NAME test_preedit_format COMMAND test_preedit_format)

if(HAVE_SYSTRAY)
    add_executable(test_tray_icon_pixmap
        test_tray_icon_pixmap.c
        ${CMAKE_SOURCE_DIR}/src/server/tray/icon_pixmap.c
    )
    target_include_directories(test_tray_icon_pixmap PRIVATE
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/src/server/tray
        ${CAIRO_PANGO_INCLUDE_DIRS}
    )
    target_link_libraries(test_tray_icon_pixmap PRIVATE ${CAIRO_PANGO_LIBRARIES})
    add_test(NAME test_tray_icon_pixmap COMMAND test_tray_icon_pixmap)

endif()

if(HAVE_STATUS_BUS)
    add_executable(test_status_bus
        test_status_bus.c
        ${CMAKE_SOURCE_DIR}/src/server/status/status.c
    )
    target_include_directories(test_status_bus PRIVATE
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/src/server
        ${CMAKE_SOURCE_DIR}/src/lib
        ${DBUS_INCLUDE_DIRS}
    )
    target_link_libraries(test_status_bus PRIVATE typio-core ${DBUS_LIBRARIES})
    add_test(NAME test_status_bus COMMAND test_status_bus)
endif()

if(BUILD_CONTROL_PANEL)
    add_executable(test_control_config
        test_control_config.c
        ${CMAKE_SOURCE_DIR}/src/control/main.c
        ${CMAKE_SOURCE_DIR}/src/control/control_config.c
        ${CMAKE_SOURCE_DIR}/src/control/control_ui.c
        ${CMAKE_SOURCE_DIR}/src/control/control_window.c
        ${CMAKE_SOURCE_DIR}/src/control/control_widgets.c
        ${CMAKE_SOURCE_DIR}/src/control/control_models.c
        ${CMAKE_SOURCE_DIR}/src/control/control_bind.c
    )
    target_compile_definitions(test_control_config PRIVATE TYPIO_CONTROL_TEST)
    target_include_directories(test_control_config PRIVATE
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/src/control
        ${CMAKE_SOURCE_DIR}/src/lib
        ${GTK4_INCLUDE_DIRS}
    )
    target_compile_options(test_control_config PRIVATE ${GTK4_CFLAGS_OTHER})
    target_link_libraries(test_control_config PRIVATE typio-core ${GTK4_LIBRARIES})
    add_test(NAME test_control_config COMMAND test_control_config)
endif()

add_executable(test_startup_guard
    test_startup_guard.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/startup_guard.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/boundary_bridge.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/lifecycle_model.c
)
target_include_directories(test_startup_guard PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_SOURCE_DIR}/src/server/wayland
)
add_test(NAME test_startup_guard COMMAND test_startup_guard)

add_executable(test_startup_health
    test_startup_health.c
    ${CMAKE_SOURCE_DIR}/src/server/notify/health.c
)
target_include_directories(test_startup_health PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_SOURCE_DIR}/src/server/notify
)
target_link_libraries(test_startup_health PRIVATE typio-core)
add_test(NAME test_startup_health COMMAND test_startup_health)

add_executable(test_boundary_bridge
    test_boundary_bridge.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/boundary_bridge.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/lifecycle_model.c
)
target_include_directories(test_boundary_bridge PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_SOURCE_DIR}/src/server/wayland
)
add_test(NAME test_boundary_bridge COMMAND test_boundary_bridge)

add_executable(test_repeat_guard
    test_repeat_guard.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/repeat_guard.c
)
target_include_directories(test_repeat_guard PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_SOURCE_DIR}/src/server/wayland
)
add_test(NAME test_repeat_guard COMMAND test_repeat_guard)

add_executable(test_key_arbiter
    test_key_arbiter.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/key_arbiter.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/shortcut_chord.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/shortcut_config.c
)
target_include_directories(test_key_arbiter PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/src/server/wayland-protocols
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_SOURCE_DIR}/src/server
    ${CMAKE_SOURCE_DIR}/src/server/tray
    ${CMAKE_SOURCE_DIR}/src/server/voice
    ${CMAKE_SOURCE_DIR}/src/server/wayland
    ${DBUS_INCLUDE_DIRS}
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
)
target_link_libraries(test_key_arbiter PRIVATE typio-core)
add_test(NAME test_key_arbiter COMMAND test_key_arbiter)

add_executable(test_shortcut_chord
    test_shortcut_chord.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/shortcut_chord.c
)
target_include_directories(test_shortcut_chord PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_SOURCE_DIR}/src/server/wayland
    ${XKBCOMMON_INCLUDE_DIRS}
)
add_test(NAME test_shortcut_chord COMMAND test_shortcut_chord)

add_executable(test_modifier_policy
    test_modifier_policy.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/modifier_policy.c
)
target_include_directories(test_modifier_policy PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_SOURCE_DIR}/src/server/wayland
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
)
add_test(NAME test_modifier_policy COMMAND test_modifier_policy)

add_executable(test_key_tracking
    test_key_tracking.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/key_tracking.c
)
target_include_directories(test_key_tracking PRIVATE
    ${CMAKE_SOURCE_DIR}/src/server/wayland
)
add_test(NAME test_key_tracking COMMAND test_key_tracking)

add_executable(test_lifecycle
    test_lifecycle.c
    ${CMAKE_SOURCE_DIR}/src/server/wayland/lifecycle_model.c
)
target_include_directories(test_lifecycle PRIVATE
    ${CMAKE_SOURCE_DIR}/src/server/wayland
)
add_test(NAME test_lifecycle COMMAND test_lifecycle)

if(BUILD_RIME_ENGINE)
    add_executable(test_rime_path_expand
        test_rime_path_expand.c
        ${CMAKE_SOURCE_DIR}/src/engines/rime/path_expand.c
    )
    target_include_directories(test_rime_path_expand PRIVATE
        ${CMAKE_SOURCE_DIR}/src/lib
        ${CMAKE_SOURCE_DIR}/src/engines/rime
    )
    target_link_libraries(test_rime_path_expand PRIVATE typio-core)
    add_test(NAME test_rime_path_expand COMMAND test_rime_path_expand)

    add_executable(test_rime_engine test_rime_engine.c)
    target_link_libraries(test_rime_engine PRIVATE typio-core)
    target_compile_definitions(test_rime_engine PRIVATE
        TYPIO_TEST_RIME_ENGINE_DIR="${CMAKE_BINARY_DIR}/engines"
    )
    add_dependencies(test_rime_engine typio-engine-rime)
    add_test(NAME test_rime_engine COMMAND test_rime_engine)
endif()
