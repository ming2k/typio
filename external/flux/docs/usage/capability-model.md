# Capability Model

flux is a rendering substrate, not a retained UI framework. The application,
shell, or toolkit owns scene objects, layout, input, invalidation, animation,
asset loading, and text shaping. flux owns the final step: turning explicit 2D
drawing commands into GPU work.

## What flux Can Do

- Create a rendering context and one or more render surfaces.
- Render to a Wayland surface, a caller-provided `VkSurfaceKHR`, or an offscreen CPU-readable target.
- Record a frame-local command list through `fx_canvas`.
- Clear, fill, stroke, clip, transform, and present.
- Draw explicit path geometry: lines, rectangles, quadratic/cubic curves, and SVG-style elliptical arcs converted into path verbs.
- Fill and stroke simple paths with solid colors or gradients.
- Upload and draw images as textured quads.
- Load a font file as a render handle, rasterize glyph masks with FreeType, cache glyphs in a GPU atlas, and draw positioned glyph runs.
- Expose HarfBuzz-compatible font handles so callers can shape text above flux.

## What flux Does Not Do

- No widget tree, layout engine, style system, input routing, focus handling, accessibility tree, or retained scene graph.
- No UTF-8 paragraph shaping, line breaking, bidirectional text handling, font discovery, fallback, or emoji fallback policy.
- No SVG/XML parser, CSS parser, image file decoder, or icon theme resolver.
- No general-purpose Vulkan framework for application-owned GPU resources.

## Translation Model

The practical model is:

```text
app/toolkit state -> layout/shaping/assets -> explicit flux draw calls -> GPU
```

If an upstream library produces pixels, draw them with `fx_image`. If it
produces vector outlines, convert those outlines into `fx_path` and draw them
with `fx_fill_path` / `fx_stroke_path`. If it produces shaped glyph IDs and
positions, append them to `fx_glyph_run` and draw them with
`fx_draw_glyph_run`.
