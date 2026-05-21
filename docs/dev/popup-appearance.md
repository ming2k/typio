# Popup Appearance Development Notes

This document covers the rendering pipeline for the candidate popup (preedit + candidates + mode label), including the pixel-format traps, variable-font weight handling, and theme resolution that have caused real bugs.

---

## Pixel format: CPU buffer and WL_SHM_FORMAT_ARGB8888

The popup is painted directly into a CPU-mapped Wayland SHM buffer; there is no GPU offscreen surface or pixel readback step.

`candidate_popup_paint.cc` fills pixels as `uint32_t` values written as `0xAARRGGBB`. On a little-endian machine this lays out in memory as `B G R A` per pixel, which is exactly what `WL_SHM_FORMAT_ARGB8888` expects.

```c
// candidate_popup_paint.cc — pack_argb helper
return (0xffu << 24) | (u8(r) << 16) | (u8(g) << 8) | u8(b);
```

```c
// candidate_popup_buffer.c
buffer->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                                           WL_SHM_FORMAT_ARGB8888);
```

**Historical note:** flux v0.2.3 rendered into a GPU offscreen surface and read back via `flux_surface_read_pixels`, which always returned RGBA8 bytes — swapping R and B relative to what `WL_SHM_FORMAT_ARGB8888` expects. The workaround was `WL_SHM_FORMAT_ABGR8888`. The current CPU path writes the correct byte order directly, so `ARGB8888` is used without any swap.

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

The cached `FT_Face` is borrowed by `TypioTextLayout` and used at draw time: `typio_flux_draw_layout` calls `FT_Load_Glyph` per glyph on each render call. Layouts must not outlive their owning font cache entry; `popup_render_ctx_invalidate` frees all layouts before the cache can be evicted.

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
