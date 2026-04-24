# Wayland Integration

## Ownership split

flux takes a "bring your own surface" approach:

| Object | Owner |
|---|---|
| `wl_display` | **host application** |
| `wl_surface` | **host application** |
| `VkSurfaceKHR` | **flux** (created from the above two) |
| `VkSwapchainKHR` | **flux** |

The host sets up a Wayland connection and a shell surface
(`xdg_toplevel`, `zwlr_layer_surface`, etc.) without flux's
involvement. Once a `wl_surface` exists, the host hands it to:

```c
fx_surface *s = fx_surface_create_wayland(ctx, display, surface,
                                          w, h, FX_CS_SRGB);
```

flux then owns the Vulkan presentation chain and drives submission.
The host continues to own and dispatch the Wayland connection.

This split means flux has no dependency on xdg-shell, layer-shell, or
any other shell protocol. The examples use xdg-shell as a practical
choice; you can use any protocol that gives you a `wl_surface`.

## Shell protocols in examples

The `examples/hello_rect.c` example uses `xdg_wm_base` /
`xdg_toplevel`. The generated client code comes from
`wayland-scanner` at build time (see `examples/meson.build`):

```
xdg-shell.xml
  → xdg-shell-client-protocol.h   (client-header mode)
  → xdg-shell-protocol.c          (private-code mode)
```

If you want a layer-shell surface (e.g. for status bars, overlays),
generate `wlr-layer-shell-unstable-v1` the same way and use
`zwlr_layer_shell_v1_get_layer_surface` instead of
`xdg_wm_base_get_xdg_surface`. Nothing changes on the flux side.

## Resize flow

1. xdg-toplevel fires a `configure` event with a new size.
2. App calls `xdg_surface_ack_configure`, then
   `fx_surface_resize(s, new_w, new_h)`.
3. flux sets `needs_recreate = true`.
4. On the next `fx_surface_acquire`, flux rebuilds the swapchain at
   the new extent.

```c
static void toplevel_configure(void *data, struct xdg_toplevel *tl,
                               int32_t w, int32_t h,
                               struct wl_array *states) {
    app_t *a = data;
    if (w > 0) a->width  = w;
    if (h > 0) a->height = h;
}

// in the event loop, after dispatching:
if (a->width != last_w || a->height != last_h) {
    fx_surface_resize(vs, a->width, a->height);
    last_w = a->width;
    last_h = a->height;
}
```

If the compositor sends a `configure` with `(0, 0)` that means
"choose your own size"; keep the current size.

## HiDPI / output scale

Wayland announces output scale factors via `wl_output.scale` (integer
scale, protocol version 2+) or, for fractional scaling,
`wp-fractional-scale-v1` (a newer Wayland extension).

flux accepts a `scale` parameter at surface creation time (planned for
phase 1). The projection matrix at the GPU level is:

```
ortho(0, logical_w, 0, logical_h)  ×  scale
```

so all draw coordinates stay in logical pixels regardless of output
scale. Glyph atlases key on `(face, size_logical × scale)` so a 14-px
font on a 2× display rasterizes into 28-px glyphs.

When you receive an `output.scale` change:

```c
// re-create or resize the fx_surface with the new scale
fx_surface_set_scale(s, new_scale);  // phase 1+
```

Until `fx_surface_set_scale` ships, call
`fx_surface_destroy` + `fx_surface_create_wayland` with the new scale.

## Composite alpha

Some compositors (particularly those that implement transparency or
blur-behind effects) may require `VK_COMPOSITE_ALPHA_PRE_MULTIPLIED`
rather than `VK_COMPOSITE_ALPHA_OPAQUE`. flux queries
`VkSurfaceCapabilitiesKHR.supportedCompositeAlpha` and prefers
`OPAQUE`, falling back to `INHERIT` → `PRE_MULTIPLIED` →
`POST_MULTIPLIED` as supported. No user configuration is needed.

## Frame pacing

Wayland's recommended pattern is:

1. Receive a `wl_surface.frame` callback.
2. Render and commit.
3. Queue the next frame callback before committing.

flux phase 0 does not integrate with `wl_surface.frame` callbacks.
The frame loop in `hello_rect.c` is a busy loop gated by the Vulkan
`FIFO_RELAXED` present mode, which naturally throttles to the
compositor's refresh rate. Phase 4 will add an explicit frame-callback
integration path for strict frame pacing.

## Tested compositors

| Compositor | Status | Notes |
|---|---|---|
| Niri | Working | Phase-0 tested |
| Sway (wlroots) | Expected | Not yet tested |
| GNOME (Mutter) | Expected | Not yet tested |
| KDE (KWin) | Expected | Not yet tested |
| Headless wlroots | CI target | Phase-0 CI not yet set up |

## Vulkan extension

`vkCreateWaylandSurfaceKHR` is looked up at runtime via
`vkGetInstanceProcAddr` rather than being linked statically. This
keeps the library loadable on systems without a Wayland session (the
function pointer will be NULL in that case and surface creation will
fail cleanly rather than crashing at link time).
