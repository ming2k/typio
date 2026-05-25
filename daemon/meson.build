# ── Typio daemon (typio-daemon) ───────────────────────────────────────────

# ── Wayland protocol generation ───────────────────────────────────────────
protocols = []
if enable_wayland
    protocol_xmls = {
        'input-method-unstable-v2': meson.project_source_root() / 'protocols' / 'input-method-unstable-v2.xml',
        'virtual-keyboard-unstable-v1': meson.project_source_root() / 'protocols' / 'virtual-keyboard-unstable-v1.xml',
        'fractional-scale-v1': meson.project_source_root() / 'protocols' / 'fractional-scale-v1.xml',
        'viewporter': meson.project_source_root() / 'protocols' / 'viewporter.xml',
    }

    foreach name, xml : protocol_xmls
        header = custom_target(name + '-client-header',
            output: name + '-client-protocol.h',
            input: xml,
            command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
        )
        code = custom_target(name + '-protocol-code',
            output: name + '-protocol.c',
            input: xml,
            command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
        )
        protocols += [header, code]
    endforeach
endif

# ── Source files ──────────────────────────────────────────────────────────
daemon_sources = files(
    'main.c',
    'app.c',
    'state/controller.c',
    'cli.c',
    'notify/health.c',
    'ipc/tip_protocol.c',
    'ipc/tip_json.c',
    'ipc/uds_server.c',
    'ipc/ipc_bus.c',
    'state/service.c',
)

if enable_wayland
    daemon_sources += files(
        'wayland/boundary_bridge.c',
        'wayland/candidate_guard.c',
        'wayland/frontend_aux.c',
        'wayland/identity.c',
        'wayland/key_arbiter.c',
        'wayland/key_debug.c',
        'wayland/key_tracking.c',
        'wayland/key_route.c',
        'wayland/keyboard_repeat.c',
        'wayland/lifecycle.c',
        'wayland/lifecycle_model.c',
        'wayland/lifecycle_state.c',
        'wayland/modifier_policy.c',
        'wayland/wl_event_loop.c',
        'wayland/wl_runtime_config.c',
        'wayland/wl_watchdog.c',
        'wayland/keyboard_xkb.c',
        'wayland/aux_handler.c',
        'wayland/aux_adapters.c',
        'wayland/text_ui_backend.c',
        'wayland/text_ui_state.c',
        'wayland/reconcile_model.c',
        'wayland/reconciler.c',
        'wayland/reconnect_backoff.c',
        'wayland/repeat_guard.c',
        'wayland/resume_signal.c',
        'wayland/shortcut_chord.c',
        'wayland/shortcut_config.c',
        'wayland/startup_guard.c',
        'wayland/vk_bridge.c',
        'wayland/wl_frontend.c',
        'wayland/preedit_format.c',
        'wayland/wl_input_method.c',
        'wayland/wl_keyboard.c',
        'wayland/wl_trace.c',
    )

    if flux_dep.found()
        daemon_sources += files(
            'wayland/popup/candidate_panel.cc',
            'wayland/popup/candidate_panel_layout.cc',
            'wayland/popup/candidate_panel_paint.cc',
            'wayland/flux_renderer.c',
            'wayland/popup/candidate_panel_theme.c',
        )
    else
        daemon_sources += files(
            'wayland/popup/candidate_panel_stub.c',
        )
    endif
endif

if enable_status_bus
    daemon_sources += files(
        'state/dbus.c',
    )
endif

if enable_systray
    daemon_sources += files(
        'tray/icon_pixmap.cc',
        'tray/bus.c',
        'tray/sni.c',
    )
endif

if build_whisper or build_sherpa_onnx
    daemon_sources += files(
        'voice/pw_capture.c',
    )
endif

# ── Dependencies ──────────────────────────────────────────────────────────
daemon_deps = [typio_core_dep]

if enable_wayland
    daemon_deps += [wayland_client_dep, xkbcommon_dep]
    if flux_dep.found()
        daemon_deps += flux_dep
    endif
    fontconfig_dep = dependency('fontconfig', required: true)
    harfbuzz_dep = dependency('harfbuzz', required: true)
    freetype_dep = dependency('freetype2', required: true)
    daemon_deps += [fontconfig_dep, harfbuzz_dep, freetype_dep]
endif

if enable_status_bus or enable_systray
    daemon_deps += dbus_dep
endif

if build_whisper or build_sherpa_onnx
    daemon_deps += pipewire_dep
endif

# ── Include directories ───────────────────────────────────────────────────
daemon_inc = [
    top_inc,
    include_directories('.'),
    include_directories('notify'),
    include_directories('state'),
    include_directories('../core/include'),
]

if enable_wayland
    daemon_inc += include_directories('wayland')
    daemon_inc += include_directories('wayland-protocols')
endif

if enable_systray
    daemon_inc += include_directories('tray')
endif

if build_whisper or build_sherpa_onnx
    daemon_inc += include_directories('voice')
    daemon_inc += include_directories('../core/src/voice')
endif

# ── Executable ────────────────────────────────────────────────────────────
typio_daemon = executable('typio-daemon',
    daemon_sources, protocols,
    dependencies: daemon_deps,
    include_directories: daemon_inc,
    install: true,
    install_dir: libexecdir,
    build_rpath: meson.current_build_dir() / '..' / 'core',
    install_rpath: '$ORIGIN/../' + libdir,
)
