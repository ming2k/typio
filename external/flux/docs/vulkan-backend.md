# Vulkan Backend

This document covers the Vulkan objects flux owns, the choices made in structuring them, and the invariants that keep the codebase correct.

## Baseline

Vulkan 1.2. No 1.3 features are required.

## Memory Management

flux uses a custom dynamic memory manager designed for high-performance 2D rendering.

### Linear Buffer Pool (`src/vk/memory.c`)
Instead of fixed vertex limits, flux implements a per-frame linear allocator (`fx_vbuf_pool`) for `HOST_VISIBLE | HOST_COHERENT` memory.
- **Dynamic Growth:** Starts at 4 MiB per frame. If the recording exceeds this, the pool allocates a new chunk (doubling the size) and chains it.
- **Retirement:** Old chunks are retired and destroyed only after the frame's fence signals, ensuring GPU safety.
- **Alignment:** All allocations are 16-byte aligned.

### Image Memory
User images and the Glyph Atlas are allocated in `DEVICE_LOCAL` memory for maximum sampling performance. flux uses staging buffers and one-time-submit command buffers to upload CPU pixel data to the GPU.

## Draw Call Batching

The renderer automatically batches primitives to minimize Vulkan overhead.
- **Grouping:** Sequential drawing operations (`fx_fill_path`, `fx_stroke_path`) are grouped into a single `vkCmdDraw` if they share the same `fx_paint` properties (color, etc.).
- **Flushing:** A batch is "flushed" (issued to the GPU) only when:
    1. The paint color changes.
    2. An operation requires a different pipeline (e.g., switching from Path to Image).
    3. The linear buffer chunk is full.
    4. The recording ends.

## Pipeline Architecture

flux uses specialized pipelines for different primitive types:

| Pipeline | Vertex Format | Push Constants | Descriptor Sets |
|---|---|---|---|
| **Solid** | `pos[2]` | `surface_size`, `color` | None |
| **Image** | `pos[2]`, `uv[2]` | `surface_size` | 1 (Sampler + Image) |
| **Text** | `pos[2]`, `uv[2]` | `surface_size`, `color` | 1 (Sampler + Atlas) |

- **Vertex Input:** All pipelines use `TRIANGLE_LIST` topology.
- **Blending:** All pipelines are configured for `SRC_OVER` alpha blending (`ONE`, `ONE_MINUS_SRC_ALPHA`).
- **Push Constants:** Used for frequently changing global state like viewport size and primitive-specific color to avoid UBO overhead.

## Glyph Atlas

The **Glyph Atlas** is a context-global resource managed in `src/text.c`.
- **Format:** 2048x2048 `A8_UNORM` (single channel alpha).
- **Algorithm:** Shelf-packing allocator. It adds 2-pixel padding between glyphs to prevent bilinear filtering artifacts (bleeding).
- **Updates:** If a glyph is missing during recording, flux immediately renders it via FreeType and issues a `vkCmdCopyBufferToImage` to update the atlas before the frame is submitted.

## Descriptor Management

flux uses a **Per-Frame Descriptor Pool** strategy:
- Each of the two in-flight frames has its own `VkDescriptorPool`.
- These pools are reset at the start of each frame (`vkResetDescriptorPool`).
- Descriptor sets for images and the glyph atlas are allocated on-the-fly during recording. This avoids complex descriptor set recycling and eliminates synchronization bottlenecks between the CPU and GPU.

## Synchronization

flux follows a standard two-frame in-flight architecture:
- **Fences:** `in_flight` fences ensure the CPU doesn't start recording frame `N` until the GPU has finished executing the previous iteration of frame `N`.
- **Semaphores:** `image_available` and `render_finished` semaphores coordinate the handoff between the swapchain acquire, the graphics queue, and the presentation engine.

## Physical Device Selection

`fx_device_init` scores devices based on type (Discrete > Integrated > Virtual). It requires:
1. `VK_KHR_swapchain` support.
2. A queue family supporting both Graphics and Presentation to the target surface.
3. Support for `A8_UNORM` and `RGBA8_UNORM` image formats.
