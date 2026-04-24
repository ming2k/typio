# Application Architecture

Keep one layer above flux that translates application concepts into flux
commands:

```text
app state
  -> layout and scene update
  -> asset cache: decoded images, parsed SVG, resolved fonts
  -> text cache: shaped glyph runs
  -> render pass: emit flux commands
```

## Responsibilities

- The app owns long-lived scene objects and decides what changed.
- The app or toolkit computes rectangles, transforms, text lines, and z-order.
- Asset loaders decode PNG/JPEG/SVG/font choices into pixels, paths, or glyph runs.
- flux resources are cached near the renderer: `fx_image` for decoded pixels, `fx_font` for resolved font files, reusable `fx_path` for vector shapes.
- Each frame, the renderer acquires a canvas, emits explicit drawing commands in z-order, then presents.

## Recommended Flow

```text
event loop
  -> update app state
  -> run layout/text shaping/asset resolution if dirty
  -> acquire flux canvas
  -> emit explicit drawing commands
  -> present
```

The boundary is intentional: once data is reduced to pixels, paths, paints,
transforms, images, or glyph runs, flux is responsible for rendering it.
