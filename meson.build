project('typio', 'c', 'cpp',
    version: '4.2.2',
    default_options: [
        'c_std=c23',
        'cpp_std=c++17',
        'warning_level=2',
        'werror=false',
    ],
    meson_version: '>=1.0.0',
)

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

# ── Global compile flags ──────────────────────────────────────────────────
add_project_arguments(
    '-D_POSIX_C_SOURCE=200809L',
    language: ['c', 'cpp'],
)

# Warning flags
warn_flags = [
    '-Wformat=2', '-Wconversion', '-Wsign-conversion',
    '-Wshadow', '-Wundef', '-Wunused', '-Wno-unused-parameter',
]
foreach f : warn_flags
    if cc.has_argument(f)
        add_project_arguments(f, language: 'c')
    endif
    if cxx.has_argument(f)
        add_project_arguments(f, language: 'cpp')
    endif
endforeach

# Sanitizers
if get_option('enable_asan')
    add_project_arguments('-fsanitize=address', '-fno-omit-frame-pointer', language: ['c', 'cpp'])
    add_project_link_arguments('-fsanitize=address', language: ['c', 'cpp'])
endif
if get_option('enable_ubsan')
    add_project_arguments('-fsanitize=undefined', '-fno-omit-frame-pointer', language: ['c', 'cpp'])
    add_project_link_arguments('-fsanitize=undefined', language: ['c', 'cpp'])
endif

# ── Options ───────────────────────────────────────────────────────────────
build_control_panel = get_option('build_control_panel')
build_tests = get_option('build_tests')
build_basic_engine = get_option('build_basic_engine')
build_rime_engine = get_option('build_rime_engine')
build_mozc_engine = get_option('build_mozc_engine')
enable_wayland = get_option('enable_wayland')
enable_status_bus = get_option('enable_status_bus')
enable_systray = get_option('enable_systray')
build_whisper = get_option('build_whisper')
build_sherpa_onnx = get_option('build_sherpa_onnx')

# ── Dependencies ──────────────────────────────────────────────────────────
thread_dep = dependency('threads')
dl_dep = cc.find_library('dl', required: false)
m_dep = cc.find_library('m', required: false)

# Wayland
if enable_wayland
    wayland_client_dep = dependency('wayland-client', required: true)
    xkbcommon_dep = dependency('xkbcommon', required: true)
    wayland_scanner = find_program('wayland-scanner', required: true)
endif

# D-Bus
if enable_status_bus or enable_systray
    dbus_dep = dependency('dbus-1', required: true)
endif

# GTK4 (control panel)
if build_control_panel
    gtk4_dep = dependency('gtk4', required: true)
    glib_compile_resources = find_program('glib-compile-resources', required: true)
endif

# Rime
if build_rime_engine
    rime_dep = dependency('rime', required: true)
    # TODO: query pkg-config for rime pkgdatadir if needed
endif

# Voice (PipeWire)
if build_whisper or build_sherpa_onnx
    pipewire_dep = dependency('libpipewire-0.3', required: true)
endif

# Whisper
whisper_dep = dependency('', required: false)
whisper_link_libs = []
if build_whisper
    whisper_dep = dependency('whisper', required: true)
    # whisper may need extra libs for static linking
    ggml = cc.find_library('ggml', required: false)
    ggml_base = cc.find_library('ggml-base', required: false)
    ggml_cpu = cc.find_library('ggml-cpu', required: false)
    gomp = cc.find_library('gomp', required: false)
    stdcpp = cc.find_library('stdc++', required: false)
    foreach lib : [ggml, ggml_base, ggml_cpu, gomp, stdcpp]
        if lib.found()
            whisper_link_libs += lib
        endif
    endforeach
endif

# Sherpa-ONNX
sherpa_onnx_dep = dependency('', required: false)
if build_sherpa_onnx
    sherpa_onnx_prefix = get_option('prefix')
    sherpa_onnx_lib = cc.find_library('sherpa-onnx-c-api',
        dirs: [sherpa_onnx_prefix / 'lib'],
        required: true)
    sherpa_onnx_inc = include_directories(sherpa_onnx_prefix / 'include')
    sherpa_onnx_dep = declare_dependency(
        dependencies: sherpa_onnx_lib,
        include_directories: sherpa_onnx_inc,
    )
endif

# Protobuf (Mozc)
if build_mozc_engine
    protoc = find_program('protoc', required: true)
endif

# ── flux external dependency ──────────────────────────────────────────────
# flux is a Meson-based static library. Check sibling directory first,
# then fall back to subprojects/flux.wrap (git clone).
flux_sibling = meson.current_source_dir() / '..' / 'flux' / 'meson.build'
flux_subproject_link = meson.current_source_dir() / 'subprojects' / 'flux'
if run_command('test', '-f', flux_sibling, check: false).returncode() == 0
    if run_command('test', '-L', flux_subproject_link, check: false).returncode() != 0
        run_command('ln', '-sf', '../../flux', flux_subproject_link, check: false)
    endif
endif

flux_dep = dependency('', required: false)
flux_proj = subproject('flux',
    default_options: [
        'canvas=true',
        'scene=false',
        'compute=true',
        'examples=false',
        'tests=false',
        'default_library=static',
    ],
    required: false,
)
if flux_proj.found()
    flux_dep = flux_proj.get_variable('flux_dep')
else
    flux_dep = dependency('flux', required: false)
endif

# ── Version & source label ────────────────────────────────────────────────
typio_version = meson.project_version()
git = find_program('git', required: false)
if git.found()
    r = run_command(git, 'describe', '--always', '--dirty', '--tags', check: false)
    if r.returncode() == 0
        typio_source_label = r.stdout().strip()
    else
        typio_source_label = 'source'
    endif
else
    typio_source_label = 'source'
endif

# ── Global include directories ────────────────────────────────────────────
top_inc = include_directories('.')

# ── Install paths ─────────────────────────────────────────────────────────
prefix = get_option('prefix')
bindir = get_option('bindir')
libdir = get_option('libdir')
includedir = get_option('includedir')
datadir = get_option('datadir') / 'typio'
enginedir = libdir / 'typio' / 'engines'
libexecdir = get_option('libexecdir') / 'typio'
autostartdir = prefix / 'etc' / 'xdg' / 'autostart'
if prefix == '/usr'
    autostartdir = '/etc/xdg/autostart'
endif

# ── Build config header ───────────────────────────────────────────────────
conf_data = configuration_data()
conf_data.set('TYPIO_VERSION', typio_version)
conf_data.set('TYPIO_VERSION_MAJOR', typio_version.split('.')[0])
conf_data.set('TYPIO_VERSION_MINOR', typio_version.split('.')[1])
conf_data.set('TYPIO_VERSION_PATCH', typio_version.split('.')[2])
conf_data.set('TYPIO_BUILD_SOURCE_LABEL', typio_source_label)
conf_data.set('TYPIO_INSTALL_PREFIX', prefix)
conf_data.set('TYPIO_ENGINE_DIR', prefix / libdir / 'typio' / 'engines')
conf_data.set('TYPIO_DATA_DIR', prefix / datadir)
conf_data.set('TYPIO_INSTALL_ICON_DIR', prefix / datadir / 'icons')
conf_data.set('TYPIO_SOURCE_ICON_DIR', meson.project_source_root() / 'data' / 'icons')
conf_data.set('BUILD_BASIC_ENGINE', build_basic_engine)
conf_data.set('BUILD_RIME_ENGINE', build_rime_engine)
conf_data.set('BUILD_MOZC_ENGINE', build_mozc_engine)
conf_data.set('HAVE_WAYLAND', enable_wayland)
conf_data.set('HAVE_STATUS_BUS', enable_status_bus)
conf_data.set('HAVE_SYSTRAY', enable_systray)
conf_data.set('HAVE_VOICE', build_whisper or build_sherpa_onnx)
conf_data.set('HAVE_WHISPER', build_whisper)
conf_data.set('HAVE_SHERPA_ONNX', build_sherpa_onnx)

configure_file(
    input: 'typio_build_config.h.in',
    output: 'typio_build_config.h',
    configuration: conf_data,
)

# ── pkg-config file ───────────────────────────────────────────────────────
pkg = import('pkgconfig')
pkg.generate(
    name: 'typio',
    description: 'Typio input method framework',
    version: typio_version,
)

# ── Subdirectories ────────────────────────────────────────────────────────
subdir('core')
subdir('engines')
subdir('daemon')
if build_control_panel
    subdir('control')
endif
subdir('cli')
if build_tests
    subdir('tests')
endif

# ── Data files ────────────────────────────────────────────────────────────
desktop_conf = configuration_data()
desktop_conf.set('TYPIO_DAEMON_DIR', prefix / get_option('libexecdir') / 'typio')

configure_file(
    input: 'data/applications/typio.desktop.in',
    output: 'typio.desktop',
    configuration: desktop_conf,
    install: true,
    install_dir: get_option('datadir') / 'applications',
)

if build_control_panel
    install_data('data/applications/typio-control.desktop',
        install_dir: get_option('datadir') / 'applications',
    )
endif

configure_file(
    input: 'data/autostart/typio.desktop.in',
    output: 'typio-autostart.desktop',
    configuration: desktop_conf,
    install: true,
    install_dir: autostartdir,
)

configure_file(
    input: 'data/typio.service.in',
    output: 'typio.service',
    configuration: desktop_conf,
    install: true,
    install_dir: prefix / 'lib' / 'systemd' / 'user',
)

install_data('data/typio.toml.example',
    install_dir: datadir,
)

install_subdir('data/icons/hicolor',
    install_dir: get_option('datadir') / 'icons',
)
