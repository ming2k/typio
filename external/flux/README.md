# flux

Low-level 2D graphics foundation on Vulkan. C11, meson, Wayland.

Status: **Phase 2 (Text Rendering)** — Full 2D vector primitives, dynamic memory management, batched Vulkan execution, and alpha-blended text rendering via dynamic GPU atlases.

## Features

- **Vector Primitives:** Concave path filling and stroking with SVG-grade caps and joins. (flux renders paths; it does not parse SVG documents.)
- **Transformation:** Full 3x3 affine matrix stack with CPU-side resolution-independent flattening.
- **Text Rendering:** FreeType glyph rasterization and HarfBuzz-ready glyph runs.
- **Dynamic Atlas:** 2048x2048 dynamic GPU glyph cache with shelf-packing.
- **Vulkan Backend:** Automatic draw call batching and per-frame dynamic ring buffers.
- **Images:** Automated GPU upload and hardware-accelerated textured quad rendering.

For a practical capability model and application-level examples, see
`docs/usage/`.

## Dependencies

- Vulkan 1.2+
- FreeType 2
- HarfBuzz
- Wayland (optional)

## Build

    meson setup build
    ninja -C build

Options:
- `-Dwayland=auto|enabled|disabled`
- `-Dexamples=true|false`
- `-Dvalidation=auto|enabled|disabled`

## Run the Phase-0 smoke test

    WAYLAND_DISPLAY=$WAYLAND_DISPLAY ./build/examples/hello_rect

Opens an xdg-toplevel and clears it to a slowly sweeping color.
The demo records a concave filled and stroked polygon plus a filled and
stroked rectangular path so the current solid-color geometry execution
paths are exercised. Close the window or Ctrl-C to exit. Set
`FX_ENABLE_VALIDATION=1` to turn on the Vulkan validation layer.

## Layout

    include/flux/*.h   — public API headers
    src/               — implementation
    src/vk/            — Vulkan backend
    examples/          — runnable demos
    docs/              — design, API, roadmap, responsibility boundaries
