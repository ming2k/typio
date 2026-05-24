# Typio daemon

set(TYPIO_DAEMON_SOURCES
    main.c
    app.c
    state/controller.c
    cli.c
    notify/health.c
    ipc/tip_protocol.c
    ipc/tip_json.c
    ipc/uds_server.c
    ipc/ipc_bus.c
    state/service.c
)
set(TYPIO_DAEMON_INCLUDE_DIRS
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/notify
    ${CMAKE_CURRENT_SOURCE_DIR}/state
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/include
)
set(TYPIO_DAEMON_SYSTEM_INCLUDE_DIRS)
set(TYPIO_DAEMON_LIBRARIES typio-core dl)
set(TYPIO_DAEMON_OUTPUT_NAME "typio-daemon")

# Wayland frontend support
if(HAVE_WAYLAND)
    # Generate protocol code
    wayland_generate_protocol(
        PROTOCOL_XML ${CMAKE_SOURCE_DIR}/protocols/input-method-unstable-v2.xml
        GENERATED_SOURCES INPUT_METHOD_PROTOCOL_SOURCES
    )

    wayland_generate_protocol(
        PROTOCOL_XML ${CMAKE_SOURCE_DIR}/protocols/virtual-keyboard-unstable-v1.xml
        GENERATED_SOURCES VIRTUAL_KEYBOARD_PROTOCOL_SOURCES
    )

    wayland_generate_protocol(
        PROTOCOL_XML ${CMAKE_SOURCE_DIR}/protocols/fractional-scale-v1.xml
        GENERATED_SOURCES FRACTIONAL_SCALE_PROTOCOL_SOURCES
    )

    wayland_generate_protocol(
        PROTOCOL_XML ${CMAKE_SOURCE_DIR}/protocols/viewporter.xml
        GENERATED_SOURCES VIEWPORTER_PROTOCOL_SOURCES
    )

    list(APPEND TYPIO_DAEMON_SOURCES
        wayland/boundary_bridge.c
        wayland/candidate_guard.c
        wayland/frontend_aux.c
        wayland/identity.c
        wayland/key_arbiter.c
        wayland/key_debug.c
        wayland/key_tracking.c
        wayland/key_route.c
        wayland/keyboard_repeat.c
        wayland/lifecycle.c
        wayland/lifecycle_model.c
        wayland/lifecycle_state.c
        wayland/modifier_policy.c
        wayland/wl_event_loop.c
        wayland/wl_runtime_config.c
        wayland/wl_watchdog.c
        wayland/keyboard_xkb.c
        wayland/aux_handler.c
        wayland/aux_adapters.c
        wayland/popup/candidate_panel.cc
        wayland/popup/candidate_panel_layout.cc
        wayland/popup/candidate_panel_paint.cc
        wayland/flux_renderer.c
        wayland/popup/candidate_panel_theme.c
        wayland/reconcile_model.c
        wayland/reconciler.c
        wayland/reconnect_backoff.c
        wayland/repeat_guard.c
        wayland/resume_signal.c
        wayland/shortcut_chord.c
        wayland/shortcut_config.c
        wayland/startup_guard.c
        wayland/text_ui_backend.c
        wayland/text_ui_state.c
        wayland/vk_bridge.c
        wayland/wl_frontend.c
        wayland/preedit_format.c
        wayland/wl_input_method.c
        wayland/wl_keyboard.c
        wayland/wl_trace.c
        ${INPUT_METHOD_PROTOCOL_SOURCES}
        ${VIRTUAL_KEYBOARD_PROTOCOL_SOURCES}
        ${FRACTIONAL_SCALE_PROTOCOL_SOURCES}
        ${VIEWPORTER_PROTOCOL_SOURCES}
    )

    add_custom_target(typio-protocols
        DEPENDS
            ${INPUT_METHOD_PROTOCOL_SOURCES}
            ${VIRTUAL_KEYBOARD_PROTOCOL_SOURCES}
            ${FRACTIONAL_SCALE_PROTOCOL_SOURCES}
            ${VIEWPORTER_PROTOCOL_SOURCES}
    )

    list(APPEND TYPIO_DAEMON_INCLUDE_DIRS
        ${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols
        ${CMAKE_CURRENT_SOURCE_DIR}/wayland
    )

    pkg_check_modules(FREETYPE REQUIRED freetype2)

    list(APPEND TYPIO_DAEMON_SYSTEM_INCLUDE_DIRS
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
        ${FREETYPE_INCLUDE_DIRS}
    )

    list(APPEND TYPIO_DAEMON_LIBRARIES
        ${WAYLAND_CLIENT_LIBRARIES}
        ${XKBCOMMON_LIBRARIES}
        flux::flux
        fontconfig
        harfbuzz
        ${FREETYPE_LIBRARIES}
        pthread
    )
endif()

# Status D-Bus support
if(HAVE_STATUS_BUS)
    list(APPEND TYPIO_DAEMON_SOURCES
        state/dbus.c
        notify/notifications.c
    )

    list(APPEND TYPIO_DAEMON_SYSTEM_INCLUDE_DIRS
        ${DBUS_INCLUDE_DIRS}
    )

    list(APPEND TYPIO_DAEMON_LIBRARIES
        ${DBUS_LIBRARIES}
    )
endif()

# System tray support
if(HAVE_SYSTRAY)
    list(APPEND TYPIO_DAEMON_SOURCES
        tray/icon_pixmap.cc
        tray/bus.c
        tray/sni.c
    )

    list(APPEND TYPIO_DAEMON_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/tray
    )
endif()

# Voice input support (shared infrastructure)
if(HAVE_VOICE)
    list(APPEND TYPIO_DAEMON_SOURCES
        voice/voice_service.c
        voice/voice_proxy.c
        voice/pw_capture.c
    )

    list(APPEND TYPIO_DAEMON_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/voice
    )

    list(APPEND TYPIO_DAEMON_SYSTEM_INCLUDE_DIRS
        ${PIPEWIRE_INCLUDE_DIRS}
    )

    list(APPEND TYPIO_DAEMON_LIBRARIES
        ${PIPEWIRE_LIBRARIES}
        pthread
    )
endif()

# Whisper backend
if(HAVE_WHISPER)
    list(APPEND TYPIO_DAEMON_SOURCES
        voice/voice_whisper.c
        voice/voice_engine_whisper.c
    )
    list(APPEND TYPIO_DAEMON_SYSTEM_INCLUDE_DIRS ${WHISPER_INCLUDE_DIRS})
    list(APPEND TYPIO_DAEMON_LIBRARIES ${WHISPER_LINK_LIBRARIES})
endif()

# Sherpa-ONNX backend
if(HAVE_SHERPA_ONNX)
    list(APPEND TYPIO_DAEMON_SOURCES
        voice/voice_sherpa.c
        voice/voice_engine_sherpa.c
    )
    list(APPEND TYPIO_DAEMON_SYSTEM_INCLUDE_DIRS ${SHERPA_ONNX_INCLUDE_DIRS})
    list(APPEND TYPIO_DAEMON_LIBRARIES ${SHERPA_ONNX_LINK_LIBRARIES})
endif()

add_executable(daemon-bin ${TYPIO_DAEMON_SOURCES})

target_include_directories(daemon-bin PRIVATE ${TYPIO_DAEMON_INCLUDE_DIRS})
target_include_directories(daemon-bin SYSTEM PRIVATE ${TYPIO_DAEMON_SYSTEM_INCLUDE_DIRS})

if(HAVE_WHISPER AND WHISPER_LIBRARY_DIRS)
    target_link_directories(daemon-bin PRIVATE ${WHISPER_LIBRARY_DIRS})
endif()

if(HAVE_SHERPA_ONNX AND SHERPA_ONNX_PREFIX)
    target_link_directories(daemon-bin PRIVATE "${SHERPA_ONNX_PREFIX}/lib")
endif()

target_link_libraries(daemon-bin PRIVATE ${TYPIO_DAEMON_LIBRARIES})

set_target_properties(daemon-bin PROPERTIES
    OUTPUT_NAME "typio-daemon"
    BUILD_RPATH "${CMAKE_BINARY_DIR}/core"
    INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    NO_SYSTEM_FROM_IMPORTED       TRUE
)

install(TARGETS daemon-bin
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/typio
)
