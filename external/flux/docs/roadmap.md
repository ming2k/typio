# Roadmap

## Status

| Phase | Name | Status |
|---|---|---|
| 0 | Skeleton | **Shipped** |
| 0.5 | Foundation | **Shipped** |
| 1 | Primitives | **Shipped** |
| 2 | Text | **Shipped** |
| 3 | Effects | Planned |
| 4 | Polish | In progress |

---

## Phase 1 — Primitives ✓

**Goal:** Turn the foundation into an executing Vulkan 2D backend for paths, images, and basic paint state.

**Deliverables (shipped):**

- **Dynamic Memory:** Per-frame linear allocator (`fx_vbuf_pool`) with automatic growth, replacing fixed vertex limits.
- **Batching:** Automatic draw call grouping for operations with identical colors.
- **Transforms:** CPU-side affine matrix stack (`fx_save`, `fx_restore`, `fx_translate`, etc.). Immediate transformation ensures optimal path flattening quality.
- **Paint System:** Introduced `fx_paint` to encapsulate color, stroke width, caps, joins, and miter limits.
- **Advanced Stroking:** Full support for SVG-grade line caps (Butt, Round, Square) and joins (Miter, Round, Bevel) with miter fallback.
- **GPU Images:** Automated upload of CPU pixels to `DEVICE_LOCAL` memory and hardware-accelerated textured quad rendering.
- **Shaders:** Specialized GLSL pipelines for solid colors and textured quads with alpha blending.

**Status:** Completed.

---

## Phase 2 — Text ✓

**Goal:** Shaped, kerned, antialiased text rendering using standard industry tools.

**Deliverables (shipped):**

- **FreeType Integration:** Professional font loading and glyph rasterization.
- **HarfBuzz Integration:** Prepared font structures for advanced shaping runs.
- **Dynamic Glyph Atlas:** A context-wide 2048x2048 `A8_UNORM` GPU cache with shelf-packing. Automatically updates as new glyphs are needed.
- **Text Pipeline:** Specialized alpha-blended fragment shader for high-quality coverage-based text rendering.
- **Resource Management:** Per-frame descriptor set pools to manage atlas and image binding without synchronization stalls.

**Status:** Completed.

---

## Phase 3 — Effects

**Goal:** Gaussian blur and drop shadow.

**Deliverables:**

- `fx_mask_blur(sigma)` — separable horizontal + vertical pass to a ping-pong offscreen render target; composite back.
- `fx_mask_drop_shadow(dx, dy, sigma, color)` — blur the mask, offset by `(dx, dy)`, composite beneath the original shape.
- `fx_paint_set_mask_filter` — attach to any paint; executed during present.
- `examples/blur_shadow.c`.
- Golden tests: blurred rect, drop-shadowed text.

---

## Phase 4 — Polish

**Goal:** Fill the remaining gaps before 1.0.

- **Shipped:** Linear and radial gradients, rectangular and path clipping,
  `fx_surface_set_dpr` / `fx_surface_get_dpr`, and
  `fx_surface_create_offscreen` for headless testing.
- **Remaining before 1.0:** Golden-image tests, performance baselines,
  validation/memory-safety soak, SDF text for large glyphs, and broader CI
  coverage.

---

## Performance targets

| Scenario | Target | Measured on |
|---|---|---|
| First frame after `context_create` | < 50 ms | Intel Iris Xe (integrated) |
| Full-screen 4K, ~200 draw ops | < 4 ms GPU | Intel Iris Xe |
| Text run of 1000 glyphs | < 2 ms record + submit | Intel Iris Xe |

These are measured before phase 3 closes. See [testing.md](testing.md) for the benchmark methodology.
