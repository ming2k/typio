# ── Rust CLI ──────────────────────────────────────────────────────────────

cli_cargo_env = environment()
cli_cargo_env.set('CARGO_TARGET_DIR', meson.current_build_dir() / 'cargo')

cli_profile = get_option('buildtype') in ['release', 'minsize'] ? 'release' : 'debug'

cli_cargo_cmd = ['cargo', 'build']
if get_option('buildtype') in ['release', 'minsize']
    cli_cargo_cmd += '--release'
endif
cli_cargo_cmd += [
    '--manifest-path', meson.current_source_dir() / 'Cargo.toml',
    '&&', 'cp',
    meson.current_build_dir() / 'cargo' / cli_profile / 'typio',
    '@OUTPUT@',
]

cli_rust = custom_target('cli-rust',
    output: 'typio',
    command: cli_cargo_cmd,
    env: cli_cargo_env,
    install: true,
    install_dir: bindir,
    build_by_default: true,
)
