# Typio daemon

set(TYPIO_DAEMON_SOURCES
    main.c
    app.c
    cli.c
    notify/health.c
)
set(TYPIO_DAEMON_INCLUDE_DIRS
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/notify
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/include
)
set(TYPIO_DAEMON_SYSTEM_INCLUDE_DIRS)
set(TYPIO_DAEMON_LIBRARIES typio-core dl)

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
        wayland/modifier_policy.c
        wayland/wl_event_loop.c
        wayland/wl_runtime_config.c
        wayland/candidate_popup.c
        wayland/candidate_popup_buffer.c
        wayland/candidate_popup_layout.c
        wayland/candidate_popup_paint.c
        wayland/candidate_popup_theme.c
        wayland/repeat_guard.c
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
    )

    add_custom_target(typio-daemon-protocols
        DEPENDS
            ${INPUT_METHOD_PROTOCOL_SOURCES}
            ${VIRTUAL_KEYBOARD_PROTOCOL_SOURCES}
    )

    list(APPEND TYPIO_DAEMON_INCLUDE_DIRS
        ${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols
        ${CMAKE_CURRENT_SOURCE_DIR}/wayland
    )

    list(APPEND TYPIO_DAEMON_SYSTEM_INCLUDE_DIRS
        ${CAIRO_PANGO_INCLUDE_DIRS}
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
    )

    list(APPEND TYPIO_DAEMON_LIBRARIES
        ${CAIRO_PANGO_LIBRARIES}
        ${WAYLAND_CLIENT_LIBRARIES}
        ${XKBCOMMON_LIBRARIES}
        pthread
    )
endif()

# Status D-Bus support
if(HAVE_STATUS_BUS)
    list(APPEND TYPIO_DAEMON_SOURCES
        status/status.c
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
        tray/icon_pixmap.c
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

add_executable(typio-server ${TYPIO_DAEMON_SOURCES})

target_include_directories(typio-server PRIVATE ${TYPIO_DAEMON_INCLUDE_DIRS})
target_include_directories(typio-server SYSTEM PRIVATE ${TYPIO_DAEMON_SYSTEM_INCLUDE_DIRS})

if(HAVE_WHISPER AND WHISPER_LIBRARY_DIRS)
    target_link_directories(typio-server PRIVATE ${WHISPER_LIBRARY_DIRS})
endif()

if(HAVE_SHERPA_ONNX AND SHERPA_ONNX_PREFIX)
    target_link_directories(typio-server PRIVATE "${SHERPA_ONNX_PREFIX}/lib")
endif()

target_link_libraries(typio-server PRIVATE ${TYPIO_DAEMON_LIBRARIES})

set_target_properties(typio-server PROPERTIES
    OUTPUT_NAME "typio"
    BUILD_RPATH "${CMAKE_BINARY_DIR}/src/core"
    INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
)

install(TARGETS typio-server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
