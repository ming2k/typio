# ── GTK4 Control Panel ────────────────────────────────────────────────────

gresource = custom_target('typio-control-resources',
    output: 'typio-control-resources.c',
    input: 'typio-control.gresource.xml',
    command: [
        glib_compile_resources,
        '--target=@OUTPUT@',
        '--sourcedir=@CURRENT_SOURCE_DIR@',
        '--generate-source',
        '@INPUT@',
    ],
    depend_files: files('typio-control.css'),
)

control_sources = files(
    'main.c',
    'control_config.c',
    'control_models.c',
    'control_widgets.c',
    'control_window.c',
    'control_ui.c',
    'control_bind.c',
)

control_exe = executable('typio-control',
    control_sources, gresource,
    dependencies: [typio_core_dep, gtk4_dep],
    include_directories: [
        include_directories('.'),
        include_directories('../core/include'),
    ],
    install: true,
    install_dir: bindir,
)
