# Rendering

This document describes the rendering architecture of flux.

## Core Design: Mesh-based Tessellation

flux uses a **mesh-based tessellation** approach. Integrated GPUs benefit from low fragment costs and consistent driver behavior. Anti-aliasing is achieved through geometry-based coverage rather than MSAA.

## Curves and Flattening

Quadratic and cubic Béziers are flattened to line segments by recursive midpoint subdivision.
- **Scale-Aware:** Flattening happens in device space after the current transformation matrix is applied.
- **Precision:** The error bound is fixed at `0.25px` in device coordinates, ensuring curves remain smooth regardless of the logical scale.

## Strokes

Strokes are expanded CPU-side into fill polygons by the stroker (`src/stroker.c`).
- **Caps:** `BUTT`, `SQUARE`, and `ROUND`.
- **Joins:** `MITER`, `ROUND`, and `BEVEL`.
- **Miter Limit:** Implemented with automatic fallback to Bevel joins to prevent geometry spikes at sharp angles.

## Text Rendering

flux uses a multi-stage pipeline for high-performance text:
1. **Shaping:** HarfBuzz is used to convert UTF-8 strings into glyph IDs and positions (upstream logic).
2. **Rasterization:** FreeType rasterizes glyphs into 8-bit alpha masks.
3. **Dynamic Atlas:** A 2048x2048 `A8_UNORM` GPU texture stores frequently used glyphs. The atlas uses a shelf-packing algorithm.
4. **Execution:** Each glyph in a run is drawn as a textured quad. The fragment shader multiplies the paint color by the atlas alpha channel.

## Memory and Batching

### Per-Frame Ring Buffers
flux uses a per-frame linear allocator (`fx_vbuf_pool`) for vertex data.
- **No Static Limits:** The buffer grows dynamically by doubling its size if the current frame complexity exceeds its capacity.
- **Zero Stall:** Two frames are kept in flight; while the GPU is rendering frame N, the CPU can begin recording frame N+1.

### Automatic Batching
Sequential operations are grouped into the minimum possible number of Vulkan draw calls.
- **Grouping Criteria:** Sequential ops that share the same `fx_paint` color and pipeline type (e.g., Multiple `fill_path` calls with the same color).
- **Flushing:** A batch is flushed when a pipeline state change is required (e.g., switching from Path to Image) or when the paint properties change.

## Pipelines and Shaders

| Pipeline | Purpose | Blend Mode |
|---|---|---|
| **Solid** | Standard path fills and strokes. | SRC_OVER |
| **Image** | Textured quads for `fx_image`. | SRC_OVER |
| **Text** | Alpha-blended glyph quads. | SRC_OVER |

Shaders are written in GLSL and compiled to SPIR-V at build time. No runtime compilation is required.

## Anti-Aliasing (AA)

Currently, flux relies on the coverage-based transparency provided by the FreeType rasterizer for text. Path AA is currently implemented through high-precision flattening; future versions will implement "fringe" geometry AA for paths to achieve sub-pixel smoothness without MSAA.

## Status

As of **Phase 2**, the solid-color, image, and text pipelines are fully operational. Path flattening, stroking, and simple polygon triangulation are performed on the CPU, with the resulting meshes batched and submitted to the GPU.
