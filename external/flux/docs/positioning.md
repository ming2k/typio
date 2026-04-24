# Positioning

flux is a **low-level 2D graphics substrate** for a Wayland-first UI
stack built on Vulkan.

It is meant to sit in the middle of the stack:

```text
widgets / app chrome / shell policy
text layout, shaping, fallback (Pango / HarfBuzz / custom)
-----------------------------------------------
flux: images, paths, glyph runs, frame recording
-----------------------------------------------
Vulkan, Wayland, allocators, queues, swapchains
```

## Mission

- Give a personal Wayland environment a small, explicit GPU graphics
  core instead of forcing the whole stack through a browser engine or a
  heavyweight retained toolkit.
- Keep the public model close to the machine: surfaces, images, path
  verbs, glyph ids, command recording, and explicit presentation.
- Make image work, vector work, and text rendering share one backend so
  shell chrome, app UI, icons, and effects live on the same pipeline.

## Design center

flux is optimized for:

- Linux + Wayland as the primary environment.
- Vulkan as the execution backend.
- Personal shell and desktop software where low latency, predictable
  ownership, and direct control matter more than cross-platform reach.
- Composition of upstream building blocks instead of rebuilding them all
  inside one monolith.

## What flux is

- A surface and frame orchestration layer over Vulkan.
- A home for GPU-oriented 2D resources: images, path geometry, glyph
  runs, atlases, pipelines, transient buffers.
- A low-level recorder that a shell, immediate-mode UI, or retained
  scene layer can target.

## What flux is not

- Not a widget toolkit.
- Not a layout engine.
- Not a text shaping engine.
- Not a browser engine.
- Not an SVG document parser.
- Not a general 3D renderer.

## Relationship to existing projects

- **Broad 2D renderers**: reference point for breadth and GPU-first 2D
  rendering. flux follows the idea of a unified image/vector/text backend,
  but it stays narrower and more explicit.
- **Cairo**: reference point for a stable drawing vocabulary. flux
  borrows the idea of explicit path construction, but aims at Vulkan and
  modern GPU execution instead of a broad backend matrix.
- **Pango / HarfBuzz**: these belong above flux. They shape text and
  choose fonts; flux consumes the resulting glyph stream and renders it.

## Why the abstraction stays low

The project is intended to be an engine layer for one environment, not
an all-purpose UI platform. Once the abstraction climbs too high, it
starts deciding layout, fallback, caching policy, or widget behavior for
the caller. flux deliberately stops below that line.

For the concrete API and module boundary contract, see
[responsibility-boundaries.md](responsibility-boundaries.md).
