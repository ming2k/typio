# Typio Server

set(TYPIO_SERVER_SOURCES main.c)
set(TYPIO_SERVER_INCLUDE_DIRS
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/lib
)
set(TYPIO_SERVER_SYSTEM_INCLUDE_DIRS)
set(TYPIO_SERVER_LIBRARIES typio-core dl)

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

    list(APPEND TYPIO_SERVER_SOURCES
        wayland/boundary_bridge.c
        wayland/key_arbiter.c
        wayland/key_debug.c
        wayland/key_tracking.c
        wayland/key_route.c
        wayland/keyboard_repeat.c
        wayland/lifecycle.c
        wayland/lifecycle_model.c
        wayland/modifier_policy.c
        wayland/popup.c
        wayland/repeat_guard.c
        wayland/shortcut_chord.c
        wayland/shortcut_config.c
        wayland/startup_guard.c
        wayland/vk_bridge.c
        wayland/wl_frontend.c
        wayland/preedit_format.c
        wayland/wl_input_method.c
        wayland/wl_keyboard.c
        wayland/wl_trace.c
        ${INPUT_METHOD_PROTOCOL_SOURCES}
        ${VIRTUAL_KEYBOARD_PROTOCOL_SOURCES}
    )

    list(APPEND TYPIO_SERVER_INCLUDE_DIRS
        ${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols
        ${CMAKE_CURRENT_SOURCE_DIR}/wayland
    )

    list(APPEND TYPIO_SERVER_SYSTEM_INCLUDE_DIRS
        ${CAIRO_PANGO_INCLUDE_DIRS}
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
    )

    list(APPEND TYPIO_SERVER_LIBRARIES
        ${CAIRO_PANGO_LIBRARIES}
        ${WAYLAND_CLIENT_LIBRARIES}
        ${XKBCOMMON_LIBRARIES}
    )
endif()

# Status D-Bus support
if(HAVE_STATUS_BUS)
    list(APPEND TYPIO_SERVER_SOURCES
        status/status.c
    )

    list(APPEND TYPIO_SERVER_SYSTEM_INCLUDE_DIRS
        ${DBUS_INCLUDE_DIRS}
    )

    list(APPEND TYPIO_SERVER_LIBRARIES
        ${DBUS_LIBRARIES}
    )
endif()

# System tray support
if(HAVE_SYSTRAY)
    list(APPEND TYPIO_SERVER_SOURCES
        tray/icon_pixmap.c
        tray/bus.c
        tray/sni.c
    )

    list(APPEND TYPIO_SERVER_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/tray
    )
endif()

# Voice input support (shared infrastructure)
if(HAVE_VOICE)
    list(APPEND TYPIO_SERVER_SOURCES
        voice/voice_service.c
        voice/pw_capture.c
    )

    list(APPEND TYPIO_SERVER_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/voice
    )

    list(APPEND TYPIO_SERVER_SYSTEM_INCLUDE_DIRS
        ${PIPEWIRE_INCLUDE_DIRS}
    )

    list(APPEND TYPIO_SERVER_LIBRARIES
        ${PIPEWIRE_LIBRARIES}
        pthread
    )
endif()

# Whisper backend
if(HAVE_WHISPER)
    list(APPEND TYPIO_SERVER_SOURCES voice/voice_whisper.c)
    list(APPEND TYPIO_SERVER_SYSTEM_INCLUDE_DIRS ${WHISPER_INCLUDE_DIRS})
    list(APPEND TYPIO_SERVER_LIBRARIES ${WHISPER_LINK_LIBRARIES})
endif()

# Sherpa-ONNX backend
if(HAVE_SHERPA_ONNX)
    list(APPEND TYPIO_SERVER_SOURCES voice/voice_sherpa.c)
    list(APPEND TYPIO_SERVER_SYSTEM_INCLUDE_DIRS ${SHERPA_ONNX_INCLUDE_DIRS})
    list(APPEND TYPIO_SERVER_LIBRARIES ${SHERPA_ONNX_LINK_LIBRARIES})
endif()

add_executable(typio-server ${TYPIO_SERVER_SOURCES})

target_include_directories(typio-server PRIVATE ${TYPIO_SERVER_INCLUDE_DIRS})
target_include_directories(typio-server SYSTEM PRIVATE ${TYPIO_SERVER_SYSTEM_INCLUDE_DIRS})

if(HAVE_WHISPER AND WHISPER_LIBRARY_DIRS)
    target_link_directories(typio-server PRIVATE ${WHISPER_LIBRARY_DIRS})
endif()

if(HAVE_SHERPA_ONNX AND SHERPA_ONNX_PREFIX)
    target_link_directories(typio-server PRIVATE "${SHERPA_ONNX_PREFIX}/lib")
endif()

target_link_libraries(typio-server PRIVATE ${TYPIO_SERVER_LIBRARIES})

set_target_properties(typio-server PROPERTIES
    OUTPUT_NAME "typio"
    BUILD_RPATH "${CMAKE_BINARY_DIR}/src/lib"
    INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
)

install(TARGETS typio-server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
