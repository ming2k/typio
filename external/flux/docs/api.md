# API Reference

This document describes the public API of flux.

For application-level integration examples, including linking, system fonts,
and SVG ingestion, see [usage/](usage/).

## Ownership

flux follows a simple own/borrow split:

- Objects returned by `*_create` are owned by the caller and destroyed with the matching `*_destroy`.
- `fx_canvas` is borrowed. It is valid only from `fx_surface_acquire(s)` until the matching `fx_surface_present(s)`.
- Recorded canvas ops borrow the resource objects they point at (`fx_image`, `fx_path`, `fx_font`, `fx_glyph_run`). You must keep them alive until the frame is presented.
- Internal copies: When recording paths under a non-identity transformation matrix, flux creates an internal transformed copy. The canvas manages the lifecycle of these internal copies automatically.

## Context

```c
fx_context_desc desc = {
    .app_name = "shell-ui",
    .enable_validation = false,
};
fx_context *ctx = fx_context_create(&desc);
```

`fx_context` owns the Vulkan instance, device selection, and the context-wide **Glyph Atlas**.

Destroy the context and all associated resources when done:

```c
fx_context_destroy(ctx);
```

### Device capabilities

Query device capabilities after the first surface has triggered device initialization:

```c
fx_device_caps caps;
fx_context_get_device_caps(ctx, &caps);
```

Fields include `validation_enabled`, `max_image_dimension_2d`, `max_color_attachments`, and `api_version`.

The core header does not expose Vulkan types. Include the Vulkan interop header
when integrating with external Vulkan code:

```c
#include <flux/flux_vulkan.h>

VkInstance inst = fx_context_get_instance(ctx);
```

## Surfaces

### Wayland surface

```c
#include <flux/flux_wayland.h>

fx_surface *surface = fx_surface_create_wayland(ctx,
    wl_display,
    wl_surface,
    1280, 720,
    FX_CS_SRGB);
```

### Generic Vulkan surface

For integration with an externally-created `VkSurfaceKHR`:

```c
#include <flux/flux_vulkan.h>

fx_surface *surface = fx_surface_create_vulkan(ctx, vk_surface,
                                               width, height, FX_CS_SRGB);
```

### Offscreen surface

Headless rendering to CPU-readable memory:

```c
fx_surface *surface = fx_surface_create_offscreen(ctx, 256, 256,
                                                  FX_FMT_RGBA8_UNORM,
                                                  FX_CS_SRGB);
```

After presenting, read pixels back:

```c
uint8_t pixels[256 * 256 * 4];
fx_surface_read_pixels(surface, pixels, 256 * 4);
```

### Frame lifecycle

```c
fx_canvas *c = fx_surface_acquire(surface);
/* record drawing commands ... */
fx_surface_present(surface);
```

Destroy a surface and all per-frame resources:

```c
fx_surface_destroy(surface);
```

### Resize

For swapchain surfaces, request a resize (takes effect on the next `fx_surface_acquire`):

```c
fx_surface_resize(surface, 1920, 1080);
```

### Device Pixel Ratio

```c
fx_surface_set_dpr(surface, 2.0f);   /* HiDPI */
float dpr = fx_surface_get_dpr(surface);
```

## Matrix Transformations

flux uses a 3x3 affine transformation stack. Transformations are applied on the CPU immediately during recording. This ensures that path flattening and stroking always happen at the final device resolution.

```c
fx_save(c);
fx_translate(c, 100, 100);
fx_rotate(c, M_PI / 4);
fx_scale(c, 2.0, 2.0);

// Draw something transformed...

fx_restore(c);
```

Available functions:
- `fx_save` / `fx_restore`: Push/pop the matrix stack.
- `fx_translate`, `fx_scale`, `fx_rotate`: Combine a new transform into the current matrix.
- `fx_concat`: Multiply the current matrix by a provided `fx_matrix`.
- `fx_set_matrix` / `fx_get_matrix`: Direct access to the current matrix.

Matrix math utilities:
- `fx_matrix_multiply(out, a, b)`: Compose two matrices.
- `fx_matrix_transform_point(m, &x, &y)`: Transform a single point.

## Color

Colors are `0xAARRGGBB` premultiplied alpha:

```c
fx_color orange = fx_color_rgba(255, 128, 0, 255); // non-premul -> premul
```

## Paint System

The `fx_paint` object encapsulates all styling information for primitives.

```c
fx_paint paint;
fx_paint_init(&paint, fx_color_rgba(255, 128, 0, 255)); // Solid orange

paint.stroke_width = 4.0f;
paint.line_cap = FX_CAP_ROUND;
paint.line_join = FX_JOIN_ROUND;
paint.miter_limit = 4.0f;
```

Available enums:
- `fx_line_cap`: `FX_CAP_BUTT`, `FX_CAP_ROUND`, `FX_CAP_SQUARE`.
- `fx_line_join`: `FX_JOIN_MITER`, `FX_JOIN_ROUND`, `FX_JOIN_BEVEL`.

### Gradients

Attach a gradient to a paint for gradient fills:

```c
fx_gradient *grad = fx_gradient_create_linear(ctx, &(fx_linear_gradient_desc){
    .start = { 0, 0 },
    .end   = { 100, 0 },
    .colors = { 0xFFFF0000, 0xFF0000FF },
    .stops  = { 0.0f, 1.0f },
    .stop_count = 2,
});

fx_paint_set_gradient(&paint, grad);
fx_fill_path(c, path, &paint);

fx_gradient_destroy(grad);
```

Radial gradients are created with `fx_gradient_create_radial` using a center point and radius.

## Drawing Primitives

### Paths

`fx_path` is an explicit verb/point stream.

```c
fx_path *path = fx_path_create();
fx_path_move_to(path, 10.0f, 10.0f);
fx_path_line_to(path, 110.0f, 10.0f);
fx_path_close(path);

fx_fill_path(c, path, &paint);
fx_stroke_path(c, path, &paint);
```

Available functions:
- `fx_path_move_to`, `fx_path_line_to`, `fx_path_quad_to`, `fx_path_cubic_to`, `fx_path_close`
- `fx_path_add_rect` — convenience for an axis-aligned rectangle.
- `fx_path_arc_to` — approximates an SVG elliptical arc as a sequence of cubic Bézier curves.
- `fx_path_reset` — clear all verbs and points without freeing the path object.
- `fx_path_destroy` — free the path and all internal data.

Query path geometry:
- `fx_path_get_bounds(path, &rect)` — compute the bounding box.
- `fx_path_verb_count(path)` — number of verb commands.
- `fx_path_point_count(path)` — number of control points.

Transform a path on the CPU:
- `fx_path_transform(src, &matrix)` — returns a new `fx_path*`; the original is untouched.

> **Fill limitations:** `fx_fill_path` tessellates simple polygons (concave is OK).
> Self-intersecting paths and the even-odd fill rule are not yet supported.
>
> **Path lifetime:** `fx_fill_path` and `fx_stroke_path` record a *reference* to the
> path; they do **not** copy it. The path must remain valid until the frame is
> presented (`fx_surface_present`). Callers should destroy paths *after* present,
> or use `fx_path_destroy` inside the canvas-reset hook if ownership is transferred.

### Rect fills

Convenience for axis-aligned rectangles without creating a path:

```c
fx_fill_rect(c, &(fx_rect){ 10, 10, 100, 50 }, fx_color_rgba(0, 0, 255, 255));
```

### Clipping

Restrict subsequent draws to a rectangular region using scissor testing. The clip rect is transformed by the current matrix.

```c
fx_clip_rect(c, &(fx_rect){ 10, 10, 100, 100 });
fx_fill_rect(c, &(fx_rect){ 0, 0, 200, 200 }, fx_color_rgba(255, 0, 0, 255));
// Only the 100x100 region at (10,10) is red

fx_reset_clip(c);
```

Path clipping uses the stencil buffer (bounding-box scissor approximation):

```c
fx_path *clip_path = fx_path_create();
fx_path_move_to(clip_path, 0, 0);
fx_path_line_to(clip_path, 100, 0);
fx_path_line_to(clip_path, 0, 100);
fx_path_close(clip_path);
fx_clip_path(c, clip_path);
```

### Images

`fx_image` handles GPU-resident pixel data.

```c
fx_image_desc img_desc = {
    .width  = 64,
    .height = 64,
    .format = FX_FMT_RGBA8_UNORM,
    .data   = pixels, // Optional initial CPU data
};

fx_image *img = fx_image_create(ctx, &img_desc);
fx_draw_image(c, img, NULL, &(fx_rect){ 0, 0, 64, 64 });
```

`fx_draw_image_ex(c, img, src, dst)` provides the same functionality and is the underlying implementation for `fx_draw_image`.

Image management:
- `fx_image_destroy(img)` — free GPU resources.
- `fx_image_update(img, data, stride)` — upload new pixel data.
- `fx_image_get_desc(img, &desc)` — query current descriptor.
- `fx_image_data(img, &size, &stride)` — access the optional CPU-side pixel copy.

Supported formats: `FX_FMT_BGRA8_UNORM`, `FX_FMT_RGBA8_UNORM`, `FX_FMT_A8_UNORM`.

### Text

flux consumes positioned glyph runs. High-level shaping (UTF-8 to glyph-ids) should be performed via HarfBuzz.

```c
fx_font *font = fx_font_create(ctx, &(fx_font_desc){
    .family = "Noto Sans",
    .source_name = "/path/to/font.ttf",
    .size = 16.0f,
});

fx_glyph_run *run = fx_glyph_run_create(8);
fx_glyph_run_append(run, glyph_id, x, y);

fx_draw_glyph_run(c, font, run, 10, 20, &paint);
```

Font management:
- `fx_font_destroy(font)`
- `fx_font_get_desc(font, &desc)` — query the descriptor used at creation.
- `fx_font_get_hb_font(font)` — borrow the underlying HarfBuzz font handle.
- `fx_font_get_metrics(font, &ascender, &descender)` — pixel metrics. `ascender > 0`, `descender <= 0`.

Glyph run management:
- `fx_glyph_run_destroy(run)`
- `fx_glyph_run_reset(run)` — clear all glyphs without freeing.
- `fx_glyph_run_count(run)` — number of glyphs.
- `fx_glyph_run_data(run)` — pointer to the glyph array.

## Canvas introspection

- `fx_canvas_op_count(c)` — number of recorded operations.
- `fx_clear(c, color)` — set the background clear color for the frame.

## Vulkan Implementation Status

- **Memory:** Per-frame dynamic ring buffers (`HOST_VISIBLE | HOST_COHERENT`).
- **Batching:** Automatic grouping of sequential primitives with identical paint properties.
- **Atlas:** Dynamic 2048x2048 `A8` glyph atlas.
- **Pipelines:** Specialized shaders for solid geometry, textured quads, alpha-blended text, and gradients.
- **Blending:** Full support for `SRC_OVER` alpha blending.
- **Clipping:** Scissor-based rectangular clipping.
