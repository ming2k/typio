/*
 * flux — Wayland surface constructor.
 *
 * The caller owns the wl_display and wl_surface. The library owns the
 * VkSurfaceKHR and the swapchain attached to it.
 */
#ifndef FLUX_WAYLAND_H
#define FLUX_WAYLAND_H

#include "flux.h"

struct wl_display;
struct wl_surface;

#ifdef __cplusplus
extern "C" {
#endif

FX_API fx_surface *fx_surface_create_wayland(fx_context      *ctx,
                                               struct wl_display *display,
                                               struct wl_surface *surface,
                                               int32_t           width,
                                               int32_t           height,
                                               fx_color_space    cs);

#ifdef __cplusplus
}
#endif

#endif /* FX_WAYLAND_H */
