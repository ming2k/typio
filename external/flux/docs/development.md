# Development

## Prerequisites

| Tool | Minimum | Why |
|---|---|---|
| GCC or Clang | GCC 12 / Clang 15 | C11 + GNU extensions |
| Meson | 1.3 | build system |
| Ninja | 1.10 | build backend |
| Vulkan SDK | 1.2 | headers + loader |
| wayland-client | 1.20 | Wayland IPC |
| wayland-protocols | 1.25 | xdg-shell XML |
| wayland-scanner | (with wayland-client) | code generation |
| FreeType | 2.13 | font rasterization (phase 2) |
| HarfBuzz | 8 | text shaping (phase 2) |
| glslangValidator | any recent | shader compilation (phase 1) |
| python3 | 3.8 | bin2c script (phase 1) |

On Arch Linux:

```
pacman -S vulkan-devel wayland wayland-protocols \
          freetype2 harfbuzz glslang python meson ninja
```

On Ubuntu / Debian:

```
apt install libvulkan-dev libwayland-dev wayland-protocols \
            libfreetype-dev libharfbuzz-dev glslang-tools \
            python3 meson ninja-build
```

## Build

```sh
meson setup build
ninja -C build
```

Useful option combinations:

```sh
# release build
meson setup build --buildtype=release

# build only the library, no examples
meson setup build -Dexamples=false

# force Wayland off (offscreen-only context)
meson setup build -Dwayland=disabled

# compile-in Vulkan validation layer support
meson setup build -Dvalidation=enabled
```

Re-configuring without wiping the build directory:

```sh
meson configure build -Dexamples=true
ninja -C build
```

## Running examples

Phase 0 has one example: `hello_rect`. It requires an active Wayland
compositor session (WAYLAND_DISPLAY must be set).

```sh
build/examples/hello_rect
```

The window shows a slowly colour-cycling background. Close with the
window manager or Ctrl-C.

## Validation layers

```sh
FX_ENABLE_VALIDATION=1 build/examples/hello_rect
```

This enables `VK_LAYER_KHRONOS_validation` and a debug messenger that
routes validation messages through the library's log sink to `stderr`.
Any `ERROR`-severity validation message indicates a driver-contract
violation in the library.

Make sure the validation layers are installed:

```sh
# Arch
pacman -S vulkan-validation-layers

# Ubuntu
apt install vulkan-validationlayers
```

If the layer is not installed, flux logs at `INFO` level that
validation was requested but the layer was not found, then continues
without it.

## Debugging

### Verbose logging

There is no runtime log-level toggle in phase 0. To get debug-level
messages, add `FX_LOG_DEBUG` calls in the area of interest and
rebuild. A runtime flag (`FX_LOG_LEVEL=debug`) is planned for phase 1.

### GDB / LLDB

Both work normally — flux is a standard shared library. Build with
`--buildtype=debug` for full symbols and no optimisation:

```sh
meson setup build-debug --buildtype=debug
ninja -C build-debug
gdb --args build-debug/examples/hello_rect
```

### RenderDoc

RenderDoc can capture Vulkan frames from any flux application. Launch
via the RenderDoc GUI or:

```sh
renderdoccmd capture -c capture.rdc build/examples/hello_rect
```

RenderDoc is particularly useful for inspecting the render pass clear
colour and, in phase 1+, the tessellated geometry and atlas textures.

### Vulkan layers beyond validation

- `VK_LAYER_LUNARG_api_dump` — logs every Vulkan call to stdout.
  Useful for verifying submit ordering.
- `VK_LAYER_MESA_overlay` — GPU frame time overlay on the rendered
  output (Mesa only).

Activate any layer via the environment:

```sh
VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_api_dump build/examples/hello_rect
```

## Incremental development loop

For tight iteration on a single file:

```sh
ninja -C build src/libflux.so && build/examples/hello_rect
```

Ninja's dependency graph means only the modified TU recompiles.

## Shader development (phase 1+)

Shaders live in `src/shaders/`. After editing a `.glsl` file:

```sh
ninja -C build       # runs glslangValidator and bin2c automatically
```

SPIR-V files land in `build/src/shaders/`. The `bin2c.py` script
embeds them into `shaders_embedded.c` as `static const uint32_t[]`
arrays.

To inspect a compiled shader:

```sh
spirv-dis build/src/shaders/solid.frag.spv
```

## Install

```sh
meson setup build --prefix=/usr/local
ninja -C build install
```

This installs `libflux.so`, the public headers under `include/flux/`,
and `flux.pc` for pkg-config. After installation:

```sh
pkg-config --cflags --libs flux
```
