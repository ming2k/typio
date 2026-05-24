# ── Rust core crate (typio-core) ──────────────────────────────────────────
# Builds the Rust staticlib via cargo, then wraps it into a shared library
# together with the optional built-in basic engine sources.

core_src = files()
if build_basic_engine
    core_src += files(
        '../engines/basic/basic.c',
        '../engines/basic/compose.c',
    )
endif

# Determine cargo profile from Meson buildtype
if get_option('buildtype') in ['release', 'minsize']
    cargo_profile = 'release'
    cargo_flag = '--release'
else
    cargo_profile = 'debug'
    cargo_flag = ''
endif

cargo_env = environment()
cargo_env.set('CARGO_TARGET_DIR', meson.current_build_dir() / 'cargo')
cargo_env.set('TYPIO_VERSION', typio_version)
cargo_env.set('TYPIO_BUILD_SOURCE_LABEL', typio_source_label)
cargo_env.set('TYPIO_DEFAULT_ENGINE_DIR', prefix / libdir / 'typio' / 'engines')

cargo_cmd = ['cargo', 'build', '--manifest-path', meson.current_source_dir() / 'Cargo.toml']
if cargo_flag != ''
    cargo_cmd += cargo_flag
endif

rust_features = []
if build_basic_engine
    rust_features += 'build_basic_engine'
endif
if rust_features.length() > 0
    cargo_cmd += '--features=' + ','.join(rust_features)
endif

rust_lib_path = meson.current_build_dir() / 'cargo' / cargo_profile / 'libtypio_core.a'

core_rust_lib = custom_target('typio-core-rust',
    output: 'libtypio_core.a',
    command: cargo_cmd + ['&&', 'cp', rust_lib_path, '@OUTPUT@'],
    env: cargo_env,
)

core_inc = include_directories('include')

typio_core = shared_library('typio-core',
    core_src,
    link_whole: core_rust_lib,
    dependencies: [dl_dep, thread_dep],
    include_directories: [top_inc, core_inc],
    install: true,
    version: typio_version,
)

typio_core_dep = declare_dependency(
    link_with: typio_core,
    include_directories: [top_inc, core_inc],
)

install_subdir('include/typio',
    install_dir: includedir,
)
