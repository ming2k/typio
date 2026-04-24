glslang = find_program('glslangValidator', required : true)

solid_color_vert_spv = custom_target('solid_color_vert_spv',
  input  : 'shaders/solid_color.vert',
  output : 'solid_color_vert_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_solid_color_vert_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

solid_color_frag_spv = custom_target('solid_color_frag_spv',
  input  : 'shaders/solid_color.frag',
  output : 'solid_color_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_solid_color_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

image_vert_spv = custom_target('image_vert_spv',
  input  : 'shaders/image.vert',
  output : 'image_vert_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_image_vert_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

image_frag_spv = custom_target('image_frag_spv',
  input  : 'shaders/image.frag',
  output : 'image_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_image_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

text_frag_spv = custom_target('text_frag_spv',
  input  : 'shaders/text.frag',
  output : 'text_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_text_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

gradient_vert_spv = custom_target('gradient_vert_spv',
  input  : 'shaders/gradient.vert',
  output : 'gradient_vert_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_gradient_vert_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

gradient_frag_spv = custom_target('gradient_frag_spv',
  input  : 'shaders/gradient.frag',
  output : 'gradient_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_gradient_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

stencil_frag_spv = custom_target('stencil_frag_spv',
  input  : 'shaders/stencil.frag',
  output : 'stencil_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_stencil_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

blur_frag_spv = custom_target('blur_frag_spv',
  input  : 'shaders/blur.frag',
  output : 'blur_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_blur_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

flux_sources = files(
  'context.c',
  'canvas.c',
  'image.c',
  'path.c',
  'surface.c',
  'stroker.c',
  'tess.c',
  'text.c',
  'arena.c',
  'vk/device.c',
  'vk/swapchain.c',
  'vk/memory.c',
) + [
  solid_color_vert_spv,
  solid_color_frag_spv,
  image_vert_spv,
  image_frag_spv,
  text_frag_spv,
  gradient_vert_spv,
  gradient_frag_spv,
  stencil_frag_spv,
  blur_frag_spv,
]

flux_c_args = [
  '-DFX_BUILDING=1',
  '-DVK_USE_PLATFORM_WAYLAND_KHR=1',
]

flux_lib = library('flux',
  flux_sources,
  include_directories : flux_inc,
  c_args              : flux_c_args,
  dependencies        : [vulkan_dep, threads_dep, m_dep, ft_dep, hb_dep] + wayland_deps,
  gnu_symbol_visibility : 'hidden',
  install             : true,
)

flux_dep = declare_dependency(
  link_with           : flux_lib,
  include_directories : flux_inc,
  dependencies        : [vulkan_dep, ft_dep, hb_dep],
)

install_headers('../include/flux/flux.h',         subdir : 'flux')
install_headers('../include/flux/flux_vulkan.h',  subdir : 'flux')
install_headers('../include/flux/flux_wayland.h', subdir : 'flux')
