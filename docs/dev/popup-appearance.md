# Popup Appearance Development Notes

This document covers the rendering pipeline for the candidate popup (preedit + candidates + mode label), including the pixel-format traps, variable-font weight handling, and theme resolution that have caused real bugs.

---

## GPU present pipeline

The popup presents a flux (Vulkan) swapchain **directly** onto its
`zwp_input_popup_surface_v2` `wl_surface`. There is no SHM buffer pool, no CPU
pixel buffer, and no pixel readback — the swapchain owns frame pacing and
buffering.

`candidate_popup.cc` drives the pipeline:

- `ensure_fx_surface()` creates the `VkSurfaceKHR` (`vkCreateWaylandSurfaceKHR`),
  the `flux_surface` (vsync/FIFO), a `flux_canvas`, and a small arena; it resizes
  the swapchain when the popup's physical extent (logical × scale) changes.
- `popup_present()` records one frame: `flux_surface_begin_frame` →
  `flux_canvas_begin` (clear to the premultiplied background colour) →
  `popup_record` (paint) → `flux_canvas_end` → `flux_frame_submit` →
  `flux_frame_present`.
- `wl_surface_set_buffer_scale` is set from `geom->scale` so a HiDPI popup shows
  at the correct logical size and stays crisp.

```mermaid
flowchart LR
    A[candidate update] --> B["ensure_fx_surface<br/>create / resize swapchain"]
    B --> C[flux_surface_begin_frame]
    C --> D["flux_canvas_begin<br/>clear = bg colour"]
    D --> E["popup_record<br/>rects + glyph images"]
    E --> F[flux_canvas_end]
    F --> G[flux_frame_submit]
    G --> H[flux_frame_present]
```

Colours are **premultiplied RGBA**. `popup_bg_color` uses
`flux_color_rgba_premul`, and each text layout is rasterised once into a
premultiplied-RGBA `flux_image` (`FLUX_FORMAT_RGBA8_UNORM`) by
`build_layout_image` in `flux_renderer.c`, then composited with flux's default
SRC_OVER. There is no `0xAARRGGBB`/`ARGB8888` byte-order concern on this path —
flux and the WSI handle the swapchain format.

**Historical note:** earlier revisions painted into a CPU-mapped SHM buffer
(`WL_SHM_FORMAT_ARGB8888`), and before that flux rendered to a GPU offscreen
surface and read pixels back via `flux_surface_read_pixels` (which forced an
`ABGR8888` workaround). Both readback/SHM paths are gone; the popup now presents
its swapchain image directly.

---

## Present pacing and stall recovery (lock / suspend)

`popup_present` runs synchronously on the single-threaded event loop (the
`POPUP_UPDATE` stage). To keep that loop responsive when a compositor stops
releasing swapchain images — e.g. while the display is asleep or the surface is
occluded behind a lock screen — the acquire/present is **bounded and
self-recovering**. See [ADR-0006](../adr/0006-resilient-candidate-popup-present.md)
for the rationale; the mechanics:

- `flux_surface_begin_frame` is called with `POPUP_PRESENT_TIMEOUT_NS` (~32 ms)
  instead of an infinite wait. The healthy on-demand path acquires instantly, so
  this budget is only consumed during a real stall.
- On `FLUX_ERROR_TIMEOUT`, `popup_present` returns `POPUP_PRESENT_RETRY`:
  `selected`/`visible` are **not** updated and `present_retry` is set, which
  re-arms `popup_update_pending` (via
  `typio_wl_candidate_popup_present_retry_pending`) so the loop re-presents the
  same state until the compositor resumes and the visible highlight catches up.
- After `POPUP_PRESENT_RECOVER_STREAK` consecutive timeouts the swapchain is
  rebuilt with `flux_surface_resize` (to its current extent), discarding the
  per-frame semaphores left dangling by the stalled acquires.
- The same `flux_surface_resize` recovery is used for `FLUX_ERROR_SURFACE_LOST`
  (driver-reported `OUT_OF_DATE`/`SUBOPTIMAL`).

This is why a stalled present never freezes key handling: input events queue on
the Wayland fd while a frame is skipped/retried, so navigation stays correct even
while the on-screen highlight is briefly behind.

```mermaid
sequenceDiagram
    autonumber
    actor U as User keys
    participant Q as Wayland fd queue
    participant L as IME event loop
    participant P as flux present
    Note over P: compositor not releasing images<br/>(display asleep / occluded after lock or suspend)
    U->>Q: Down, Down, Down
    L->>P: popup_present → begin_frame (≤ ~32 ms)
    P-->>L: FLUX_ERROR_TIMEOUT
    Note over L: skip frame, re-arm popup_update_pending<br/>(loop is NOT blocked)
    Q->>L: deliver queued key events
    L->>L: engine advances selected correctly
    Note over L,P: on-screen highlight lags,<br/>committed word is correct
    Note over P: compositor resumes releasing images
    L->>P: popup_present (retry)
    P-->>L: FLUX_OK — highlight catches up
```

> Requires the matching flux change that maps an acquire `VK_TIMEOUT` to
> `FLUX_ERROR_TIMEOUT`. flux is built from a local sibling checkout; rebuild it
> (`ninja -C build/_flux_build install`) before rebuilding typio.

---

## Font loading and variable fonts

### Font description parsing

`parse_font_desc` in `flux_renderer.c` understands descriptions such as:

```
"Noto Sans SemiBold 16"
```

It extracts:
- family: `"Noto Sans"`
- weight: `600` (SemiBold)
- size: `16`

### Font file selection via FontConfig

`match_font_file` asks FontConfig for a file matching `(family, weight)`. For traditional static fonts this returns different files (`NotoSans-Regular.ttf`, `NotoSans-Bold.ttf`, etc.).

### The variable-font trap

Modern systems often ship **variable fonts** — a single `.ttf` file (e.g. `NotoSans-VariableFont_wdth,wght.ttf`) that contains every weight from 100 to 900. FontConfig returns this one file for *all* weights, but FreeType loads it as the **default instance** (usually Regular, `wght = 400`).

If you do not set the variable axis, asking for SemiBold (600) or Bold (700) renders identically to Regular (400).

**Fix:** after `FT_New_Face`, detect a variable font via `FT_Get_MM_Var`, find the `wght` axis, and set it with `FT_Set_Var_Design_Coordinates`:

```c
static bool set_face_weight(FT_Face face, int32_t weight)
{
    FT_MM_Var *amaster = NULL;
    FT_Fixed  *coords  = NULL;
    FT_Error   err;
    FT_UInt    i;
    bool       ok = false;

    err = FT_Get_MM_Var(face, &amaster);
    if (err != 0) return false;

    coords = (FT_Fixed *)calloc(amaster->num_axis, sizeof(FT_Fixed));
    if (!coords) goto done;

    err = FT_Get_Var_Design_Coordinates(face, amaster->num_axis, coords);
    if (err != 0) goto done;

    for (i = 0; i < amaster->num_axis; ++i) {
        if (amaster->axis[i].tag == ((FT_ULong)'w' << 24 |
                                     (FT_ULong)'g' << 16 |
                                     (FT_ULong)'h' << 8  | 't')) {
            coords[i] = (FT_Fixed)weight * 65536;  /* 16.16 fixed point */
            ok = true;
            break;
        }
    }

    if (ok) {
        err = FT_Set_Var_Design_Coordinates(face, amaster->num_axis, coords);
        ok = (err == 0);
    }

done:
    free(coords);
    FT_Done_MM_Var(ft_library, amaster);
    return ok;
}
```

Call this **before** `FT_Set_Pixel_Sizes`.

---

## Font object caching

`font_obj_cache` stores `(path, size, weight)` → `(FT_Face, hb_font_t)`. Because variable fonts need different `wght` coordinates for the same file, the cache key **must include weight**.

If you omit weight from the cache key, Medium (500) and SemiBold (600) would share the same `FT_Face` even after the variable-font fix above, because the face object itself is mutated by `FT_Set_Var_Design_Coordinates`.

The cached `FT_Face` is borrowed by `TypioTextLayout`. On a layout's **first** draw, `build_layout_image` calls `FT_Load_Glyph` per glyph once to rasterise the whole layout into a premultiplied-RGBA `flux_image`, which is then cached on the layout and reused for its lifetime (`typio_flux_fill_layout` just draws the cached image on subsequent calls). So the borrowed `FT_Face` is only touched during that one-time rasterisation, not on every render. Layouts must still not outlive their owning font cache entry; `popup_render_ctx_invalidate` frees all layouts before the cache can be evicted.

---

## Theme resolution

The popup supports three modes:

| Mode | Behaviour |
|---|---|
| `auto` | Detects desktop dark/light from GTK_THEME, gtk-3.0/4.0 settings.ini, or KDE kdeglobals |
| `light` | Built-in light palette |
| `dark` | Built-in dark palette |

The resolved palette is cached with a 5-second TTL to avoid repeated filesystem reads during rapid render cycles.

Users can override individual channels per mode via `display.colors.light.*` and `display.colors.dark.*` in the config file. The `popup_config_build_palette` function applies these overrides on top of the built-in base palette.

### When adding a new colour channel

1. Add the fields to `TypioCandidatePopupPalette` in `candidate_popup_theme.h`
2. Add defaults to `palette_light` and `palette_dark` in `candidate_popup_theme.c`
3. Add parsing support in `popup_config_load` (`LOAD_VARIANT` macro)
4. Add override application in `popup_config_build_palette`
5. Use the new colour in `candidate_popup_paint.cc`
6. Update `docs/reference/configuration.md`

---

## Layout cache invalidation

`PopupRenderCtx` maintains an LRU layout cache keyed by:
- candidate label + text
- font description
- packed 32-bit colours (label + text)

Changing the font weight, size, family, or any colour channel produces a different cache key and therefore new layouts. However, the cache does **not** survive a `popup_render_ctx_invalidate` call, which happens on theme changes or manual reloads.
