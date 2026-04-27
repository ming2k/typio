cmake_minimum_required(VERSION 3.20)

find_package(PkgConfig REQUIRED)

pkg_check_modules(FLUX_VULKAN REQUIRED vulkan)
pkg_check_modules(FLUX_FREETYPE REQUIRED freetype2)
pkg_check_modules(FLUX_HARFBUZZ REQUIRED harfbuzz)
pkg_check_modules(FLUX_WAYLAND REQUIRED wayland-client)

find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

set(FLUX_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")
set(FLUX_SHADER_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/src")

function(flux_shader output input symbol)
    add_custom_command(
        OUTPUT "${FLUX_SHADER_BIN_DIR}/${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${FLUX_SHADER_BIN_DIR}"
        COMMAND "${GLSLANG_VALIDATOR}"
                -V --target-env vulkan1.2 --quiet
                --vn "${symbol}"
                "${FLUX_SHADER_DIR}/${input}"
                -o "${FLUX_SHADER_BIN_DIR}/${output}"
        DEPENDS "${FLUX_SHADER_DIR}/${input}"
        VERBATIM
    )
endfunction()

flux_shader(solid_color_vert_spv.inc solid_color.vert fx_solid_color_vert_spv)
flux_shader(solid_color_frag_spv.inc solid_color.frag fx_solid_color_frag_spv)
flux_shader(image_vert_spv.inc       image.vert       fx_image_vert_spv)
flux_shader(image_frag_spv.inc       image.frag       fx_image_frag_spv)
flux_shader(text_frag_spv.inc        text.frag        fx_text_frag_spv)
flux_shader(gradient_vert_spv.inc    gradient.vert    fx_gradient_vert_spv)
flux_shader(gradient_frag_spv.inc    gradient.frag    fx_gradient_frag_spv)
flux_shader(stencil_frag_spv.inc     stencil.frag     fx_stencil_frag_spv)
flux_shader(blur_frag_spv.inc        blur.frag        fx_blur_frag_spv)

set(FLUX_SHADER_INCS
    "${FLUX_SHADER_BIN_DIR}/solid_color_vert_spv.inc"
    "${FLUX_SHADER_BIN_DIR}/solid_color_frag_spv.inc"
    "${FLUX_SHADER_BIN_DIR}/image_vert_spv.inc"
    "${FLUX_SHADER_BIN_DIR}/image_frag_spv.inc"
    "${FLUX_SHADER_BIN_DIR}/text_frag_spv.inc"
    "${FLUX_SHADER_BIN_DIR}/gradient_vert_spv.inc"
    "${FLUX_SHADER_BIN_DIR}/gradient_frag_spv.inc"
    "${FLUX_SHADER_BIN_DIR}/stencil_frag_spv.inc"
    "${FLUX_SHADER_BIN_DIR}/blur_frag_spv.inc"
)

add_library(flux STATIC
    src/context.c
    src/canvas.c
    src/image.c
    src/path.c
    src/surface.c
    src/stroker.c
    src/tess.c
    src/text.c
    src/arena.c
    src/vk/device.c
    src/vk/swapchain.c
    src/vk/memory.c
    src/vk/upload.c
    src/vk/vma.cpp
    ${FLUX_SHADER_INCS}
)

target_compile_features(flux PUBLIC c_std_11 cxx_std_17)
target_compile_definitions(flux
    PUBLIC
        FX_STATIC=1
    PRIVATE
        FX_BUILDING=1
        VK_USE_PLATFORM_WAYLAND_KHR=1
        _GNU_SOURCE
)

target_include_directories(flux
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vk"
        "${CMAKE_CURRENT_BINARY_DIR}/src"
)

target_include_directories(flux SYSTEM
    PRIVATE
        ${FLUX_VULKAN_INCLUDE_DIRS}
        ${FLUX_FREETYPE_INCLUDE_DIRS}
        ${FLUX_HARFBUZZ_INCLUDE_DIRS}
        ${FLUX_WAYLAND_INCLUDE_DIRS}
)

target_link_libraries(flux
    PUBLIC
        ${FLUX_VULKAN_LIBRARIES}
        ${FLUX_FREETYPE_LIBRARIES}
        ${FLUX_HARFBUZZ_LIBRARIES}
    PRIVATE
        ${FLUX_WAYLAND_LIBRARIES}
        m
        pthread
)

set_target_properties(flux PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

add_library(flux::flux ALIAS flux)
