# Responsibility Boundaries

This document defines what flux owns as a graphics library and what must stay
above or below it. It is the boundary contract for public API additions and
internal refactors.

For concrete application-level examples, see [usage/](usage/).

## Layer Model

```text
application / shell / toolkit
widgets, layout, input policy, animation policy, scene invalidation

text and asset preparation
font fallback, paragraph layout, shaping, SVG parsing, image decoding

flux core API
surfaces, frame lifecycle, canvas recording, paths, paints, images,
gradients, fonts as render handles, positioned glyph runs

flux backend integration
Vulkan instance/device/swapchain execution, Wayland surface wrapping,
offscreen render targets, GPU memory, descriptor and pipeline management

system libraries and drivers
Vulkan, Wayland, FreeType, HarfBuzz, allocators, GPU driver
```

## flux Owns

- Frame orchestration: acquire a frame-local `fx_canvas`, record commands, then submit/present with `fx_surface_present`.
- 2D drawing vocabulary: paths, fills, strokes, clips, images, gradients, and positioned glyph runs.
- Resource handles needed by the renderer: `fx_image`, `fx_path`, `fx_font`, `fx_gradient`, and `fx_glyph_run`.
- CPU-side geometry preparation that is inherently rendering work: path flattening, tessellation, stroke expansion, glyph atlas packing, and transient vertex upload.
- Backend execution details behind explicit integration headers: Vulkan device selection, queues, command buffers, render passes, swapchains, pipelines, descriptor pools, and offscreen readback.
- Predictable ownership rules: created objects are caller-owned, recorded ops borrow caller resources until present, and the canvas owns any internal transformed copies it creates.

## flux Does Not Own

- Widgets, controls, styling systems, input routing, focus policy, shell policy, or accessibility trees.
- Layout: flex/grid/box layout, paragraph layout, line breaking, text wrapping, baseline alignment, or animation/invalidation policy.
- Text shaping and fallback policy. flux may expose HarfBuzz-compatible font handles and render glyph IDs, but callers decide how Unicode text becomes positioned glyph runs.
- Document or asset parsing: SVG parsing, CSS parsing, image file decoding, font discovery, icon theme lookup, or resource loading from disk.
- General Vulkan ownership for the application. flux owns the Vulkan objects it creates; callers that pass a `VkSurfaceKHR` or use `VkInstance` interop keep responsibility for their own external objects and synchronization.
- General 3D rendering, compute workloads, cross-backend abstraction, or compositor/window-manager behavior.

## Public Header Boundaries

- `flux/flux.h` is the core graphics API. It must not require callers to include Vulkan, Wayland, or other platform headers.
- `flux/flux_wayland.h` is the Wayland convenience integration layer. It wraps caller-owned `wl_display` and `wl_surface` objects.
- `flux/flux_vulkan.h` is the Vulkan interop layer. It exposes `fx_context_get_instance` and `fx_surface_create_vulkan` for callers that intentionally integrate with Vulkan.
- New platform/backend-specific APIs should live in their own integration headers instead of expanding `flux.h`.

## Module Responsibilities

- `context.c` owns context lifetime, logging, backend initialization entry points, FreeType initialization, and context-wide caches.
- `canvas.c` owns recording only: paint defaults, transform stack, clip commands, and append-only display-list ops. It must not submit GPU work.
- `path.c`, `tess.c`, and `stroker.c` own geometry construction and CPU-side conversion of path data into renderable triangles.
- `image.c` owns image descriptors, CPU pixel copies, and GPU upload for renderer-owned images.
- `text.c` owns font handles, glyph-run containers, FreeType rasterization, and glyph atlas population. It does not choose fonts or shape paragraphs.
- `surface.c` owns surface lifetime, frame lifecycle, recorded-op execution, batching, clipping state, presentation, and offscreen readback.
- `src/vk/*` owns raw Vulkan helpers: instance/device selection, swapchain and pipeline construction, render pass setup, and transient buffer allocation.

## Change Rules

- If an API needs layout, shaping, widgets, file formats, or scene policy, it belongs above flux unless it can be expressed as explicit drawing commands.
- If an API exposes backend/platform types, put it behind a named interop header.
- If state affects rendering, it must be either public and recordable on `fx_canvas` or fully internal to execution. Do not keep planned feature state in core structs before the public contract exists.
- Recording functions validate inputs before touching canvas or resource state.
- Recording remains CPU-only. GPU commands are emitted only from surface present paths.
