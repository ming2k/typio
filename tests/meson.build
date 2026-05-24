# ── Tests ─────────────────────────────────────────────────────────────────

core_test_inc = include_directories('../core/include')
wayland_test_inc = include_directories('../daemon/wayland')
daemon_test_inc = include_directories('../daemon')

test_core = executable('test_core', 'test_core.c',
    dependencies: typio_core_dep,
)
test('core', test_core)

test_config = executable('test_config', 'test_config.c',
    dependencies: typio_core_dep,
)
test('config', test_config)

test_basic_compose = executable('test_basic_compose',
    ['test_basic_compose.c', '../engines/basic/compose.c'],
    include_directories: include_directories('../engines/basic'),
)
test('basic_compose', test_basic_compose)

test_config_schema = executable('test_config_schema', 'test_config_schema.c',
    dependencies: typio_core_dep,
)
test('config_schema', test_config_schema)

test_rime_schema_list = executable('test_rime_schema_list', 'test_rime_schema_list.c',
    dependencies: typio_core_dep,
)
test('rime_schema_list', test_rime_schema_list)

test_engine_manager = executable('test_engine_manager', 'test_engine_manager.c',
    dependencies: typio_core_dep,
    cpp_args: ['-DTYPIO_TEST_LAZY_ENGINE_PATH="' + meson.current_build_dir() + '/test_lazy_engine_plugin.so"'],
)
test('engine_manager', test_engine_manager)

test_preedit_format = executable('test_preedit_format',
    ['test_inline_ui.c', '../daemon/wayland/preedit_format.c'],
    include_directories: [core_test_inc, wayland_test_inc],
)
test('preedit_format', test_preedit_format)

test_voice_proxy = executable('test_voice_proxy',
    ['test_voice_proxy.c', '../daemon/voice/voice_proxy.c'],
    include_directories: [include_directories('../daemon/voice'), core_test_inc],
    dependencies: [thread_dep, typio_core_dep],
)
test('voice_proxy', test_voice_proxy, timeout: 30)

test_uds_ipc = executable('test_uds_ipc',
    [
        'test_uds_ipc.c',
        '../daemon/ipc/tip_protocol.c',
        '../daemon/ipc/tip_json.c',
        '../daemon/ipc/uds_server.c',
        '../daemon/ipc/ipc_bus.c',
        '../daemon/state/service.c',
        '../daemon/state/controller.c',
    ],
    include_directories: [top_inc, daemon_test_inc, core_test_inc],
    dependencies: typio_core_dep,
)
test('uds_ipc', test_uds_ipc, timeout: 30)

test_startup_guard = executable('test_startup_guard',
    ['test_startup_guard.c', '../daemon/wayland/startup_guard.c',
     '../daemon/wayland/boundary_bridge.c', '../daemon/wayland/lifecycle_model.c'],
    include_directories: [core_test_inc, wayland_test_inc],
)
test('startup_guard', test_startup_guard)

test_startup_health = executable('test_startup_health',
    ['test_startup_health.c', '../daemon/notify/health.c'],
    include_directories: [top_inc, core_test_inc, include_directories('../daemon/notify')],
    dependencies: typio_core_dep,
)
test('startup_health', test_startup_health)

test_boundary_bridge = executable('test_boundary_bridge',
    ['test_boundary_bridge.c', '../daemon/wayland/boundary_bridge.c',
     '../daemon/wayland/lifecycle_model.c'],
    include_directories: [core_test_inc, wayland_test_inc],
)
test('boundary_bridge', test_boundary_bridge)

test_repeat_guard = executable('test_repeat_guard',
    ['test_repeat_guard.c', '../daemon/wayland/repeat_guard.c'],
    include_directories: [core_test_inc, wayland_test_inc],
)
test('repeat_guard', test_repeat_guard)

test_frontend_aux = executable('test_frontend_aux',
    ['test_frontend_aux.c', '../daemon/wayland/frontend_aux.c'],
    include_directories: wayland_test_inc,
)
test('frontend_aux', test_frontend_aux)

test_vk_bridge = executable('test_vk_bridge',
    ['test_vk_bridge.c', '../daemon/wayland/vk_bridge.c', '../daemon/wayland/lifecycle_model.c'],
    include_directories: [top_inc, include_directories('../daemon/wayland-protocols'),
        core_test_inc, daemon_test_inc, include_directories('../daemon/tray'),
        include_directories('../daemon/voice'), wayland_test_inc],
    dependencies: [typio_core_dep, wayland_client_dep, xkbcommon_dep],
)
test('vk_bridge', test_vk_bridge)

test_identity = executable('test_identity',
    ['test_identity.c', '../daemon/wayland/identity.c'],
    include_directories: [top_inc, include_directories('../daemon/wayland-protocols'),
        core_test_inc, daemon_test_inc, include_directories('../daemon/tray'),
        include_directories('../daemon/voice'), wayland_test_inc],
    dependencies: typio_core_dep,
)
test('identity', test_identity)

test_candidate_guard = executable('test_candidate_guard',
    ['test_candidate_guard.c', '../daemon/wayland/candidate_guard.c'],
    include_directories: [core_test_inc, wayland_test_inc],
    dependencies: typio_core_dep,
)
test('candidate_guard', test_candidate_guard)

test_text_ui_state = executable('test_text_ui_state',
    ['test_text_ui_state.c', '../daemon/wayland/text_ui_state.c'],
    include_directories: wayland_test_inc,
)
test('text_ui_state', test_text_ui_state)

test_key_arbiter = executable('test_key_arbiter',
    ['test_key_arbiter.c', '../daemon/wayland/key_arbiter.c',
     '../daemon/wayland/shortcut_chord.c', '../daemon/wayland/shortcut_config.c'],
    include_directories: [top_inc, include_directories('../daemon/wayland-protocols'),
        core_test_inc, daemon_test_inc, include_directories('../daemon/tray'),
        include_directories('../daemon/voice'), wayland_test_inc],
    dependencies: typio_core_dep,
)
test('key_arbiter', test_key_arbiter)

test_key_route = executable('test_key_route',
    ['test_key_route.c', '../daemon/wayland/key_route.c'],
    include_directories: [top_inc, include_directories('../daemon/wayland-protocols'),
        core_test_inc, daemon_test_inc, include_directories('../daemon/tray'),
        include_directories('../daemon/voice'), wayland_test_inc],
    dependencies: [typio_core_dep, xkbcommon_dep],
)
test('key_route', test_key_route)

test_shortcut_chord = executable('test_shortcut_chord',
    ['test_shortcut_chord.c', '../daemon/wayland/shortcut_chord.c'],
    include_directories: [core_test_inc, wayland_test_inc],
)
test('shortcut_chord', test_shortcut_chord)

test_modifier_policy = executable('test_modifier_policy',
    ['test_modifier_policy.c', '../daemon/wayland/modifier_policy.c'],
    include_directories: [core_test_inc, wayland_test_inc],
)
test('modifier_policy', test_modifier_policy)

test_key_tracking = executable('test_key_tracking',
    ['test_key_tracking.c', '../daemon/wayland/key_tracking.c'],
    include_directories: wayland_test_inc,
)
test('key_tracking', test_key_tracking)

test_lifecycle = executable('test_lifecycle',
    ['test_lifecycle.c', '../daemon/wayland/lifecycle_model.c'],
    include_directories: wayland_test_inc,
)
test('lifecycle', test_lifecycle)

test_resume_model = executable('test_resume_model', 'test_resume_model.c',
    include_directories: wayland_test_inc,
)
test('resume_model', test_resume_model)

test_lifecycle_state = executable('test_lifecycle_state',
    ['test_lifecycle_state.c', '../daemon/wayland/lifecycle_state.c',
     '../daemon/wayland/lifecycle_model.c'],
    include_directories: wayland_test_inc,
)
test('lifecycle_state', test_lifecycle_state)

test_reconcile_model = executable('test_reconcile_model',
    ['test_reconcile_model.c', '../daemon/wayland/reconcile_model.c'],
    include_directories: wayland_test_inc,
)
test('reconcile_model', test_reconcile_model)

test_reconnect_backoff = executable('test_reconnect_backoff',
    ['test_reconnect_backoff.c', '../daemon/wayland/reconnect_backoff.c'],
    include_directories: wayland_test_inc,
)
test('reconnect_backoff', test_reconnect_backoff)

test_state_machine_properties = executable('test_state_machine_properties',
    ['test_state_machine_properties.c', '../daemon/wayland/reconcile_model.c',
     '../daemon/wayland/lifecycle_state.c', '../daemon/wayland/lifecycle_model.c',
     '../daemon/wayland/reconnect_backoff.c'],
    include_directories: wayland_test_inc,
)
test('state_machine_properties', test_state_machine_properties)

# Conditional tests
if enable_systray
    test_tray_icon_pixmap = executable('test_tray_icon_pixmap',
        ['test_tray_icon_pixmap.c', '../daemon/tray/icon_pixmap.cc'],
        include_directories: [top_inc, include_directories('../daemon/tray')],
        dependencies: m_dep,
    )
    test('tray_icon_pixmap', test_tray_icon_pixmap)

    test_server_app = executable('test_server_app',
        ['test_server_app.c', '../daemon/app.c', '../daemon/state/controller.c'],
        include_directories: [top_inc, daemon_test_inc, core_test_inc],
        dependencies: typio_core_dep,
        c_args: ['-DTYPIO_DAEMON_TEST'],
    )
    test('server_app', test_server_app)
endif

if enable_status_bus
    test_status_bus = executable('test_status_bus',
        ['test_status_bus.c', '../daemon/state/dbus.c', '../daemon/state/service.c',
         '../daemon/ipc/tip_json.c', '../daemon/ipc/tip_protocol.c',
         '../daemon/state/controller.c'],
        include_directories: [top_inc, daemon_test_inc, core_test_inc],
        dependencies: [typio_core_dep, dbus_dep],
    )
    test('status_bus', test_status_bus)
endif

if build_control_panel
    test_control_config = executable('test_control_config',
        ['test_control_config.c', '../control/main.c', '../control/control_config.c',
         '../control/control_ui.c', '../control/control_window.c',
         '../control/control_widgets.c', '../control/control_models.c',
         '../control/control_bind.c'],
        include_directories: [top_inc, include_directories('../control'), core_test_inc],
        dependencies: [typio_core_dep, gtk4_dep],
        c_args: ['-DTYPIO_CONTROL_TEST'],
    )
    test('control_config', test_control_config,
        env: {'DISPLAY': '', 'WAYLAND_DISPLAY': '', 'GDK_BACKEND': ''})
endif

if build_rime_engine
    test_rime_path_expand = executable('test_rime_path_expand',
        ['test_rime_path_expand.c', '../engines/rime/path_expand.c'],
        include_directories: [core_test_inc, include_directories('../engines/rime')],
        dependencies: typio_core_dep,
    )
    test('rime_path_expand', test_rime_path_expand)

    test_rime_engine = executable('test_rime_engine', 'test_rime_engine.c',
        dependencies: typio_core_dep,
        c_args: ['-DTYPIO_TEST_RIME_ENGINE_DIR="' + meson.current_build_dir() + '/../engines"'],
    )
    test('rime_engine', test_rime_engine,
        depends: rime_engine,
        timeout: 120,
        env: {'TYPIO_RIME_SYNC_DEPLOY': '1'},
        suite: 'integration',
    )
endif
