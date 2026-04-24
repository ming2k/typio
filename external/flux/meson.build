project('flux', 'c',
  version : '0.0.1',
  license : 'Apache-2.0',
  default_options : [
    'c_std=c11',
    'warning_level=2',
    'default_library=shared',
    'b_ndebug=if-release',
  ],
)

cc = meson.get_compiler('c')

add_project_arguments(
  '-D_GNU_SOURCE',
  '-Wno-unused-parameter',
  '-Wno-missing-field-initializers',
  language : 'c',
)

vulkan_dep   = dependency('vulkan',        version : '>=1.2')
ft_dep       = dependency('freetype2',     version : '>=23.0.0', required : false)
if not ft_dep.found()
  ft_dep = dependency('freetype2')
endif
hb_dep       = dependency('harfbuzz',      version : '>=2.0.0')
threads_dep  = dependency('threads')
m_dep        = cc.find_library('m', required : false)

wayland_opt  = get_option('wayland')
wayland_deps = []
wayland_enabled = false
if wayland_opt.allowed()
  wc  = dependency('wayland-client',    required : wayland_opt)
  wps = dependency('wayland-protocols', required : wayland_opt,
                   version : '>=1.25')
  wsc = find_program('wayland-scanner',  required : wayland_opt)
  if wc.found() and wps.found() and wsc.found()
    wayland_deps = [wc]
    wayland_enabled = true
    wayland_scanner = wsc
    wayland_protocols_dir = wps.get_variable(pkgconfig : 'pkgdatadir')
  endif
endif

flux_inc = include_directories('include', 'src')

subdir('src')

if get_option('examples') and wayland_enabled
  subdir('examples')
endif

if get_option('tests')
  subdir('tests')
endif

pkg = import('pkgconfig')
pkg.generate(
  libraries   : flux_lib,
  name        : 'flux',
  filebase    : 'flux',
  description : 'Low-level 2D graphics foundation on Vulkan',
  version     : meson.project_version(),
)
