# Architecture

This document describes the runtime shape of flux.

## Object model

```
┌──────────────────────┐
│ fx_context           │   one per process
│  • VkInstance        │   owns device selection and logging
│  • VkDevice          │   owns context-wide caches (pipelines, atlas)
│  • FT_Library        │   FreeType handle
│  • fx_atlas          │   dynamic GPU glyph atlas
└──────────┬───────────┘
           │ borrows
  ┌────────┴──────────────────────────────────┐
  │                                           │
┌─▼───────────────────┐          ┌────────────▼────────┐
│ fx_surface          │          │ fx_image, fx_font…  │
│  • VkSurfaceKHR     │          │ (resources, shared) │
│  • VkSwapchainKHR   │          └─────────────────────┘
│  • render pass      │
│  • frames[2]        │
│    – cmd buffer     │
│    – vbuf_pool      │
│    – desc_pool      │
│    – sync objects   │
│  • fx_canvas        │
│    – matrix stack   │
│    – recorded ops   │
└─────────────────────┘
```

- **`fx_context`** owns the core Vulkan instance, logical device, and the **Dynamic Glyph Atlas**. It serves as the primary resource manager for font loading (via FreeType).
- **`fx_surface`** manages a presentation window (Wayland), its swapchain, and two in-flight frame slots. Each frame slot is a self-contained unit of execution with its own command buffer, dynamic vertex buffer pool, and descriptor set pool.
- **`fx_canvas`** is the primary recording interface. It maintains a 3x3 transformation matrix stack and an operation list.
- **Resources** (`fx_image`, `fx_path`, `fx_font`, `fx_glyph_run`) are shared across surfaces. `fx_image` handles the transition of CPU pixel data to high-performance GPU memory.

## Per-frame Data Flow

flux uses a strict separation between **Recording** and **Execution**.

### 1. Recording (CPU-Immediate)
When drawing functions like `fx_fill_path` or `fx_draw_glyph_run` are called:
- **Transformations:** The current affine matrix is applied during recording on the CPU. Paths and path clips may be copied into transformed internal paths; glyph runs remain borrowed while their draw origin is recorded in device space.
- **Op Logging:** A `fx_op` is appended to the canvas's internal display list. If a transformation occurred, the canvas takes ownership of a new, transformed path object.
- **No GPU Work:** No Vulkan commands are issued during this phase.

### 2. Execution (GPU-Batched)
When `fx_surface_present` is called:
- **Tessellation:** Paths are flattened and tessellated into triangles.
- **Stroking:** Lines are expanded into geometry based on `fx_paint` (caps/joins).
- **Dynamic Atlas:** Missing glyphs are rasterized and uploaded to the GPU atlas.
- **Batching:** Sequential ops with identical paint properties are grouped into a single Vulkan draw call.
- **Vertex Upload:** Geometry is copied into the frame's dynamic linear buffer.
- **Submission:** A single primary command buffer is submitted to the graphics queue.

## Source layout

```
include/flux/           public headers
  flux.h                core types, matrix stack, paint, recorder
  flux_wayland.h        wayland surface constructor
  flux_vulkan.h         explicit Vulkan interop entry points

src/
  internal.h            shared internal declarations
  context.c             fx_context lifecycle, logging, FT initialization
  canvas.c              op recording, matrix stack, paint init
  image.c               GPU image creation and pixel upload
  path.c                path storage, matrix math, path transforms
  surface.c             fx_surface lifecycle, batching, VK execution
  stroker.c             SVG-grade line cap and join expansion
  tess.c                simple polygon triangulation
  text.c                FreeType/HarfBuzz glue, Dynamic Atlas manager
  vk/
    device.c            instance + device + queue selection
    memory.c            dynamic ring buffer allocator (vbuf_pool)
    swapchain.c         swapchain, render pass, and pipeline creation
  shaders/
    solid.vert/frag     solid geometry shader
    image.vert/frag     textured quad shader
    text.frag           alpha-coverage text shader

examples/               runnable demos (hello_rect)
tests/                  unit tests (transforms, stroker, foundation)
```

## Error Model

flux uses a "creation-time return, runtime-internal" error model:
- **Creation Failures:** (`fx_context_create`, etc.) return `NULL` and log the reason.
- **Resource Limits:** Dynamic growth (vbuf, atlas) is handled internally. If a hard limit is reached (e.g., GPU out of memory), the operation is dropped and a warning is logged.
- **Validation:** Integrated Vulkan validation layers route messages to the application's log handler.
