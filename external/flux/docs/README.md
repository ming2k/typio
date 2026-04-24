# flux documentation

flux is a C11 library that exposes a GPU-first 2D drawing API on top of
Vulkan. This directory is the long-form home for design decisions,
coding conventions, and contributor workflow. Short-form user docs
live in the top-level `README.md`.

## Audience

| You are… | Start with |
|---|---|
| Trying to understand what layer flux is supposed to occupy | [positioning.md](positioning.md) |
| Checking whether a proposed API belongs in flux | [responsibility-boundaries.md](responsibility-boundaries.md) |
| A first-time consumer of the library | [api.md](api.md) |
| Integrating flux into an application | [usage/](usage/) |
| Trying to build or run the project locally | [development.md](development.md) |
| Writing a pull request | [contributing.md](contributing.md) + [conventions.md](conventions.md) |
| Tracing how a `fx_draw_*` call becomes a `vkQueueSubmit` | [architecture.md](architecture.md) → [rendering.md](rendering.md) → [vulkan-backend.md](vulkan-backend.md) |
| Tracking what's shipped vs planned | [roadmap.md](roadmap.md) |

## Contents

- [architecture.md](architecture.md) — component map and per-frame data flow
- [positioning.md](positioning.md) — mission, layer boundary, and non-goals
- [responsibility-boundaries.md](responsibility-boundaries.md) — ownership and API boundary rules
- [usage/](usage/) — capability model, linking, and application-level examples split by topic
- [rendering.md](rendering.md) — tessellation, coverage AA, batching, clips, atlases
- [vulkan-backend.md](vulkan-backend.md) — instance, device, swapchain, memory, descriptors, pipelines
- [api.md](api.md) — public API reference, object model, worked examples
- [conventions.md](conventions.md) — coding, naming, ownership, threading, error model
- [wayland.md](wayland.md) — platform integration, HiDPI, shell choices
- [development.md](development.md) — building, running, debugging, validation layers
- [testing.md](testing.md) — unit, golden, integration, performance
- [contributing.md](contributing.md) — PR flow, commit style, review checklist
- [roadmap.md](roadmap.md) — phased delivery plan and current status

## Status at a glance

Phase 2 is shipping: A fully functional 2D vector graphics engine with dynamic memory management, batched Vulkan execution, high-fidelity affine transformations, professional line stroking, and dynamic GPU-atlased text rendering. See [roadmap.md](roadmap.md) for the phase-by-phase breakdown.
