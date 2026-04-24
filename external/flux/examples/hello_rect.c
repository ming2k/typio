/* hello_rect — bootstrap rendering smoke test.
 * Opens an xdg-toplevel, clears the canvas each frame, and records a
 * filled rectangular path so both the clear path and the first
 * non-clear Vulkan execution path are exercised. */

#include "flux/flux.h"
#include "flux/flux_wayland.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

typedef struct {
    struct wl_display    *display;
    struct wl_registry   *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base   *wm_base;

    struct wl_surface    *surface;
    struct xdg_surface   *xdg_surface;
    struct xdg_toplevel  *toplevel;

    int32_t width, height;
    bool    configured;
    bool    closed;
} app_t;

static void wm_base_ping(void *d, struct xdg_wm_base *wm, uint32_t serial)
{
    (void)d;
    xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void reg_global(void *data, struct wl_registry *reg, uint32_t name,
                       const char *iface, uint32_t version)
{
    app_t *a = data;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        a->compositor = wl_registry_bind(reg, name,
                                         &wl_compositor_interface,
                                         version < 4 ? version : 4);
    } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
        a->wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface,
                                      version < 3 ? version : 3);
        xdg_wm_base_add_listener(a->wm_base, &wm_base_listener, a);
    }
}
static void reg_global_remove(void *d, struct wl_registry *r, uint32_t n)
{
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener registry_listener = {
    .global        = reg_global,
    .global_remove = reg_global_remove,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xs,
                                  uint32_t serial)
{
    app_t *a = data;
    xdg_surface_ack_configure(xs, serial);
    a->configured = true;
}
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *tl,
                               int32_t w, int32_t h, struct wl_array *states)
{
    (void)tl; (void)states;
    app_t *a = data;
    if (w > 0) a->width  = w;
    if (h > 0) a->height = h;
}
static void toplevel_close(void *data, struct xdg_toplevel *tl)
{
    (void)tl;
    app_t *a = data;
    a->closed = true;
}
static void toplevel_configure_bounds(void *d, struct xdg_toplevel *t,
                                      int32_t w, int32_t h)
{ (void)d; (void)t; (void)w; (void)h; }
static void toplevel_wm_capabilities(void *d, struct xdg_toplevel *t,
                                     struct wl_array *caps)
{ (void)d; (void)t; (void)caps; }

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure        = toplevel_configure,
    .close            = toplevel_close,
    .configure_bounds = toplevel_configure_bounds,
    .wm_capabilities  = toplevel_wm_capabilities,
};

static volatile sig_atomic_t g_stop = 0;
static void sigint(int sig) { (void)sig; g_stop = 1; }

int main(void)
{
    app_t a = { .width = 800, .height = 600 };

    a.display = wl_display_connect(NULL);
    if (!a.display) {
        fprintf(stderr, "wl_display_connect failed (WAYLAND_DISPLAY unset?)\n");
        return 1;
    }
    a.registry = wl_display_get_registry(a.display);
    wl_registry_add_listener(a.registry, &registry_listener, &a);
    wl_display_roundtrip(a.display);

    if (!a.compositor || !a.wm_base) {
        fprintf(stderr, "compositor missing wl_compositor or xdg_wm_base\n");
        return 1;
    }

    a.surface     = wl_compositor_create_surface(a.compositor);
    a.xdg_surface = xdg_wm_base_get_xdg_surface(a.wm_base, a.surface);
    xdg_surface_add_listener(a.xdg_surface, &xdg_surface_listener, &a);
    a.toplevel    = xdg_surface_get_toplevel(a.xdg_surface);
    xdg_toplevel_add_listener(a.toplevel, &toplevel_listener, &a);
    xdg_toplevel_set_title(a.toplevel, "flux hello_rect");
    xdg_toplevel_set_app_id(a.toplevel, "io.flux.hello_rect");
    wl_surface_commit(a.surface);

    while (!a.configured && wl_display_dispatch(a.display) != -1) { }
    if (!a.configured) {
        fprintf(stderr, "never got initial configure\n");
        return 1;
    }

    fx_context *ctx = fx_context_create(&(fx_context_desc){
        .app_name          = "hello_rect",
        .enable_validation = false,
    });
    if (!ctx) return 1;

    fx_surface *vs = fx_surface_create_wayland(ctx, a.display, a.surface,
                                               a.width, a.height,
                                               FX_CS_SRGB);
    if (!vs) return 1;

    signal(SIGINT, sigint);

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    fx_path *rect = fx_path_create();
    fx_path *poly = fx_path_create();
    if (!rect || !poly) return 1;

    int32_t last_w = a.width, last_h = a.height;
    while (!a.closed && !g_stop) {
        if (wl_display_dispatch_pending(a.display) == -1) break;
        wl_display_flush(a.display);

        if (a.width != last_w || a.height != last_h) {
            fx_surface_resize(vs, a.width, a.height);
            last_w = a.width;
            last_h = a.height;
        }

        fx_canvas *c = fx_surface_acquire(vs);
        if (c) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            float t = (float)(now.tv_sec - t0.tv_sec)
                    + (float)(now.tv_nsec - t0.tv_nsec) * 1e-9f;
            /* Slow hue sweep so the frame is visibly animating. */
            uint8_t r = (uint8_t)(128 + 127 * __builtin_sinf(t * 0.7f));
            uint8_t g = (uint8_t)(128 + 127 * __builtin_sinf(t * 0.9f + 2.1f));
            uint8_t b = (uint8_t)(128 + 127 * __builtin_sinf(t * 1.1f + 4.2f));
            float rw = a.width * (0.22f + 0.08f * __builtin_sinf(t * 1.3f));
            float rh = a.height * (0.18f + 0.06f * __builtin_sinf(t * 1.1f + 1.4f));
            float rx = (a.width - rw) * 0.5f;
            float ry = (a.height - rh) * 0.5f;
            float cx = a.width * 0.5f;
            float cy = a.height * 0.34f;
            float pr = a.height * 0.12f;

            fx_clear(c, fx_color_rgba(r, g, b, 255));

            fx_paint p;
            fx_paint_init(&p, fx_color_rgba(255, 210, 90, 232));

            fx_path_reset(poly);
            fx_path_move_to(poly, cx - pr, cy - pr * 0.65f);
            fx_path_line_to(poly, cx + pr, cy - pr * 0.65f);
            fx_path_line_to(poly, cx + pr * 0.28f, cy);
            fx_path_line_to(poly, cx + pr, cy + pr * 0.7f);
            fx_path_line_to(poly, cx - pr, cy + pr * 0.7f);
            fx_path_line_to(poly, cx - pr * 0.28f, cy);
            fx_path_close(poly);
            fx_fill_path(c, poly, &p);

            p.color = fx_color_rgba(70, 35, 10, 220);
            p.stroke_width = 4.0f;
            fx_stroke_path(c, poly, &p);

            fx_path_reset(rect);
            fx_path_add_rect(rect, &(fx_rect){ rx, ry, rw, rh });
            
            p.color = fx_color_rgba(245, 245, 250, 220);
            fx_fill_path(c, rect, &p);

            p.color = fx_color_rgba(20, 20, 28, 220);
            p.stroke_width = 6.0f;
            fx_stroke_path(c, rect, &p);
            fx_surface_present(vs);
        }
    }

    fx_path_destroy(poly);
    fx_path_destroy(rect);
    fx_surface_destroy(vs);
    fx_context_destroy(ctx);

    xdg_toplevel_destroy(a.toplevel);
    xdg_surface_destroy(a.xdg_surface);
    wl_surface_destroy(a.surface);
    xdg_wm_base_destroy(a.wm_base);
    wl_compositor_destroy(a.compositor);
    wl_registry_destroy(a.registry);
    wl_display_disconnect(a.display);
    return 0;
}
