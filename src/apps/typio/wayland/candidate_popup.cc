/**
 * @file candidate_popup.cc
 * @brief Wayland input-popup coordinator.
 *
 * The popup presents a flux (Vulkan) swapchain directly onto its
 * zwp_input_popup_surface_v2 wl_surface. Each candidate update records the
 * popup into a flux canvas and presents one frame; the swapchain owns frame
 * pacing and buffering, so there is no SHM buffer pool or manual frame
 * throttling.
 */

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <flux/flux.h>
#include <flux/vulkan.h>

#include "wl_frontend_internal.h"
#include "candidate_popup_layout.h"
#include "candidate_popup_paint.h"
#include "candidate_popup_theme.h"
#include "flux_renderer.h"
#include "monotonic_time.h"
#include "preedit_format.h"
#include "typio/engine_label.h"
#include "typio/engine_manager.h"
#include "typio/instance.h"
#include "utils/log.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Render latency threshold for slow-render debug logging */
#define POPUP_SLOW_RENDER_MS 8

/* Bounded acquire/present timeout. The popup presents synchronously on the
 * single-threaded IME event loop, so a compositor that has stopped releasing
 * swapchain images (display asleep or surface occluded after a lock/suspend)
 * must never block the loop in vkAcquireNextImageKHR. ~2 vblanks @60Hz; the
 * healthy on-demand path acquires instantly and never approaches this. */
#define POPUP_PRESENT_TIMEOUT_NS     (32ull * 1000ull * 1000ull)
/* Recreate the swapchain after this many consecutive stalls. flux_surface_resize
 * rebuilds the chain and discards the per-frame semaphores left dangling by the
 * stalled acquires, so presentation resumes cleanly once the session is back. */
#define POPUP_PRESENT_RECOVER_STREAK 2

/* ── Output tracking ────────────────────────────────────────────────── */

typedef struct PopupOutputRef {
    struct wl_output    *output;
    struct PopupOutputRef *next;
} PopupOutputRef;

/* ── Main popup struct ──────────────────────────────────────────────── */

struct TypioWlCandidatePopup {
    TypioWlFrontend *frontend;

    /* Wayland surface objects */
    struct wl_surface                  *surface;
    struct zwp_input_popup_surface_v2  *popup_surface;

    /* flux GPU present pipeline (Vulkan swapchain on the popup wl_surface) */
    VkSurfaceKHR  vk_surface;
    flux_surface *fx_surface;
    flux_canvas  *fx_canvas;
    flux_arena    fx_arena;
    bool          fx_ready;
    int           surf_w, surf_h;   /* current swapchain extent, physical px */

    /* Present stall recovery (lock/suspend). When the compositor stops
     * releasing swapchain images, the bounded acquire times out: count the
     * consecutive stalls to drive swapchain recreation, and flag a retry so
     * the event loop re-presents once presentation resumes. */
    int  present_timeout_streak;
    bool present_retry;

    /* Per-popup text engine context + LRU layout cache */
    PopupRenderCtx render;

    /* Current computed geometry (owned; NULL if not yet rendered) */
    PopupGeometry *geom;

    /* Render configuration */
    PopupConfig config;
    bool        config_valid;

    /* Theme cache */
    TypioCandidatePopupThemeCache theme_cache;

    /* Currently displayed selection index */
    int selected;

    /* Whether the popup surface is currently visible */
    bool visible;

    /* Transient status text (e.g. "[Recording...]"). Owned; freed on destroy
     * or when status is cleared.  Phase 1 of unified panel backend. */
    char *status_text;

    /* Output tracking (for scale resolution) */
    PopupOutputRef *entered_outputs;

    /* Text-input cursor rectangle (informational; set by compositor) */
    int text_input_x, text_input_y, text_input_w, text_input_h;
};

/* ── Delta classification ───────────────────────────────────────────── */

typedef enum {
    POPUP_DELTA_NONE,
    POPUP_DELTA_SELECTION,
    POPUP_DELTA_AUX,
    POPUP_DELTA_CONTENT,
    POPUP_DELTA_STYLE,
} PopupDelta;

static PopupDelta classify_delta(const PopupGeometry *geom,
                                  const TypioCandidateList *cands,
                                  const char *preedit,
                                  const char *status,
                                  const char *mode_label,
                                  const PopupConfig *cfg,
                                  uint64_t palette_sig,
                                  int scale,
                                  int new_selected) {
    (void)new_selected;
    if (!geom) return POPUP_DELTA_CONTENT;

    if (geom->scale != scale ||
        geom->palette_sig != palette_sig ||
        geom->config.theme_mode != cfg->theme_mode ||
        geom->config.layout_mode != cfg->layout_mode ||
        geom->config.font_size != cfg->font_size ||
        geom->config.mode_indicator != cfg->mode_indicator ||
        strcmp(geom->config.font_desc, cfg->font_desc) != 0 ||
        strcmp(geom->config.aux_font_desc, cfg->aux_font_desc) != 0) {
        return POPUP_DELTA_STYLE;
    }

    if (geom->content_sig != cands->content_signature) {
        /* If count changed, it's a full content change. */
        if (geom->row_count != cands->count) {
            return POPUP_DELTA_CONTENT;
        }

        /* Without per-row signatures in the core API, we cannot prove that only
         * one row changed. Keep the conservative full-content path. */
        return POPUP_DELTA_CONTENT;
    }

    const char *cur_status = status ? status : "";
    if (strcmp(geom->status_text, cur_status) != 0) {
        /* Status changes may alter the preedit-zone size and colour,
         * so treat them as full-content rather than aux-only. */
        return POPUP_DELTA_CONTENT;
    }

    const char *cur_pre = preedit ? preedit : "";
    const char *cur_mode = mode_label ? mode_label : "";
    if (strcmp(geom->preedit_text, cur_pre) != 0 ||
        strcmp(geom->mode_label, cur_mode) != 0) {
        return POPUP_DELTA_AUX;
    }

    return POPUP_DELTA_SELECTION;
}

/* ── Output helpers ─────────────────────────────────────────────────── */

static const TypioWlOutput *find_frontend_output(const TypioWlCandidatePopup *popup,
                                                   struct wl_output *output) {
    for (TypioWlOutput *o = popup->frontend ? popup->frontend->outputs : nullptr;
         o; o = o->next) {
        if (o->output == output) return o;
    }
    return nullptr;
}

static bool tracks_output(const TypioWlCandidatePopup *popup,
                           struct wl_output *output) {
    for (PopupOutputRef *r = popup->entered_outputs; r; r = r->next) {
        if (r->output == output) return true;
    }
    return false;
}

static int render_scale(const TypioWlCandidatePopup *popup) {
    int scale = 1;
    for (PopupOutputRef *r = popup->entered_outputs; r; r = r->next) {
        const TypioWlOutput *o = find_frontend_output(popup, r->output);
        if (o && o->scale > scale) scale = o->scale;
    }
    return scale;
}

static void track_output(TypioWlCandidatePopup *popup, struct wl_output *output);
static void untrack_output(TypioWlCandidatePopup *popup, struct wl_output *output);

/* ── Mode label ─────────────────────────────────────────────────────── */

static char *build_mode_label(TypioWlCandidatePopup *popup) {
    const TypioEngineMode *mode;
    TypioEngineManager    *mgr;
    TypioEngine           *active;
    const char            *engine_name;
    const char            *engine_label;
    char buf[128];

    if (!popup || !popup->frontend || !popup->frontend->instance) return nullptr;

    mode = typio_instance_get_last_mode(popup->frontend->instance);
    if (!mode || !mode->display_label || !mode->display_label[0]) return nullptr;

    mgr          = typio_instance_get_engine_manager(popup->frontend->instance);
    active       = mgr  ? typio_engine_manager_get_active(mgr) : nullptr;
    engine_name  = active ? typio_engine_get_name(active)      : nullptr;
    engine_label = typio_engine_label_fallback(engine_name);

    if (engine_label && *engine_label) {
        snprintf(buf, sizeof(buf), "%s %s", engine_label, mode->display_label);
    } else {
        snprintf(buf, sizeof(buf), "%s", mode->display_label);
    }

    return strdup(buf);
}

/* ── Config helpers ─────────────────────────────────────────────────── */

static const PopupConfig *get_config(TypioWlCandidatePopup *popup) {
    if (!popup->config_valid) {
        popup_config_load(&popup->config,
                           popup->frontend ? popup->frontend->instance : nullptr);
        popup->config_valid = true;
    }
    return &popup->config;
}

/* ── Geometry Free Helper ───────────────────────────────────────────── */

static void render_geometry_free(PopupRenderCtx *pc, PopupGeometry *g) {
    (void)pc;
    if (!g) return;
    popup_geometry_free(g);
}

/* ── flux swapchain lifecycle ───────────────────────────────────────── */

static inline uint8_t popup_u8(double v) {
    if (v <= 0.0) return 0;
    if (v >= 1.0) return 255;
    return (uint8_t)(v * 255.0 + 0.5);
}

static flux_color popup_bg_color(const TypioCandidatePopupPalette *p) {
    return flux_color_rgba_premul(popup_u8(p->bg_r), popup_u8(p->bg_g),
                                  popup_u8(p->bg_b), popup_u8(p->bg_a));
}

static void fx_teardown(TypioWlCandidatePopup *popup) {
    if (!popup) return;

    flux_device *dev = (popup->fx_surface || popup->vk_surface) ? typio_flux_device_get() : nullptr;
    if (dev && popup->fx_ready) flux_device_wait_idle(dev);

    if (popup->fx_canvas) {
        flux_canvas_destroy(popup->fx_canvas);
        popup->fx_canvas = nullptr;
    }
    if (popup->fx_ready) {
        flux_arena_destroy(&popup->fx_arena);
    }
    if (popup->fx_surface) {
        flux_surface_release(popup->fx_surface);
        popup->fx_surface = nullptr;
    }
    if (popup->vk_surface != VK_NULL_HANDLE && dev) {
        vkDestroySurfaceKHR(flux_device_vk_instance(dev), popup->vk_surface, nullptr);
    }
    popup->vk_surface = VK_NULL_HANDLE;
    popup->fx_ready   = false;
    popup->surf_w = popup->surf_h = 0;
}

/* Create / resize the swapchain to (w, h) physical pixels. */
static bool ensure_fx_surface(TypioWlCandidatePopup *popup, int w, int h) {
    if (!popup || !popup->frontend || !popup->surface || w <= 0 || h <= 0) return false;

    flux_device *dev = typio_flux_device_get();
    if (!dev) return false;

    if (popup->vk_surface == VK_NULL_HANDLE) {
        VkWaylandSurfaceCreateInfoKHR ci = {
            .sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
            .pNext   = nullptr,
            .flags   = 0,
            .display = popup->frontend->display,
            .surface = popup->surface,
        };
        if (vkCreateWaylandSurfaceKHR(flux_device_vk_instance(dev), &ci, nullptr,
                                      &popup->vk_surface) != VK_SUCCESS) {
            popup->vk_surface = VK_NULL_HANDLE;
            return false;
        }
    }

    if (!popup->fx_surface) {
        flux_surface_desc sd = {};
        sd.type           = FLUX_TYPE_SURFACE_DESC;
        sd.vk_surface_khr = popup->vk_surface;
        sd.width          = (uint32_t)w;
        sd.height         = (uint32_t)h;
        sd.vsync          = true;
        if (flux_surface_create(dev, &sd, &popup->fx_surface) != FLUX_OK) {
            popup->fx_surface = nullptr;
            return false;
        }

        flux_canvas_desc cd = {};
        cd.type    = FLUX_TYPE_CANVAS_DESC;
        cd.surface = popup->fx_surface;
        if (flux_canvas_create(&cd, &popup->fx_canvas) != FLUX_OK) {
            popup->fx_canvas = nullptr;
            flux_surface_release(popup->fx_surface);
            popup->fx_surface = nullptr;
            return false;
        }

        if (flux_arena_init(&popup->fx_arena, 256 * 1024, nullptr) != FLUX_OK) {
            flux_canvas_destroy(popup->fx_canvas);
            popup->fx_canvas = nullptr;
            flux_surface_release(popup->fx_surface);
            popup->fx_surface = nullptr;
            return false;
        }

        popup->surf_w   = w;
        popup->surf_h   = h;
        popup->fx_ready = true;
    } else if (popup->surf_w != w || popup->surf_h != h) {
        if (flux_surface_resize(popup->fx_surface, (uint32_t)w, (uint32_t)h) == FLUX_OK) {
            popup->surf_w = w;
            popup->surf_h = h;
        }
    }

    return popup->fx_ready;
}

typedef enum {
    POPUP_PRESENT_OK,     /* frame presented */
    POPUP_PRESENT_RETRY,  /* transient stall; skip this frame, re-present later */
    POPUP_PRESENT_FAIL,   /* hard failure */
} PopupPresentResult;

/* Record + present one frame of the popup.
 *
 * The acquire/fence wait is bounded (POPUP_PRESENT_TIMEOUT_NS) so a compositor
 * that has stopped releasing swapchain images — e.g. while the display is
 * asleep or the surface is occluded behind a lock screen — cannot block the
 * single-threaded IME event loop. On a stall we return POPUP_PRESENT_RETRY and,
 * after a few consecutive stalls, recreate the swapchain (which also clears the
 * per-frame semaphores left dangling by the stalled acquires) so presentation
 * resumes cleanly once the session is back. */
static PopupPresentResult popup_present(TypioWlCandidatePopup *popup,
                                        const PopupGeometry *geom, int selected) {
    if (!popup->fx_ready || !geom || !geom->palette) return POPUP_PRESENT_FAIL;

    flux_frame_begin_desc bd = {};
    bd.type       = FLUX_TYPE_FRAME_BEGIN_DESC;
    bd.timeout_ns = POPUP_PRESENT_TIMEOUT_NS;

    flux_frame *frame = nullptr;
    flux_result r = flux_surface_begin_frame(popup->fx_surface, &bd, &frame);
    if (r == FLUX_ERROR_SURFACE_LOST) {
        (void)flux_surface_resize(popup->fx_surface,
                                  (uint32_t)popup->surf_w, (uint32_t)popup->surf_h);
        popup->present_timeout_streak = 0;
        r = flux_surface_begin_frame(popup->fx_surface, &bd, &frame);
    }
    if (r == FLUX_ERROR_TIMEOUT) {
        if (++popup->present_timeout_streak >= POPUP_PRESENT_RECOVER_STREAK) {
            /* Stalled acquires leave stale per-frame semaphores; resizing the
             * surface to its current extent rebuilds the swapchain and resets
             * them. vkDeviceWaitIdle inside resize waits on GPU work (which
             * completes regardless of presentation), so it does not block. */
            (void)flux_surface_resize(popup->fx_surface,
                                      (uint32_t)popup->surf_w, (uint32_t)popup->surf_h);
            popup->present_timeout_streak = 0;
        }
        return POPUP_PRESENT_RETRY;
    }
    if (r != FLUX_OK) return POPUP_PRESENT_FAIL;

    popup->present_timeout_streak = 0;

    flux_color clear = popup_bg_color(geom->palette);
    if (flux_canvas_begin(popup->fx_canvas, frame, &clear) != FLUX_OK) return POPUP_PRESENT_FAIL;

    PopupPaintTarget target = { popup->fx_canvas, &popup->fx_arena };
    popup_record(&target, geom, selected);

    flux_arena_reset(&popup->fx_arena);
    flux_canvas_end(popup->fx_canvas);

    if (flux_frame_submit(frame) != FLUX_OK) return POPUP_PRESENT_FAIL;

    r = flux_frame_present(frame);
    if (r == FLUX_ERROR_SURFACE_LOST) {
        (void)flux_surface_resize(popup->fx_surface,
                                  (uint32_t)popup->surf_w, (uint32_t)popup->surf_h);
        return POPUP_PRESENT_RETRY;  /* next update repaints at the new extent */
    }
    return r == FLUX_OK ? POPUP_PRESENT_OK : POPUP_PRESENT_FAIL;
}

/* ── Surface hide ───────────────────────────────────────────────────── */

static void hide_surface(TypioWlCandidatePopup *popup) {
    if (!popup || !popup->surface || !popup->visible) return;

    /* Unmap by committing a null buffer. The flux swapchain stays alive so a
     * later show only needs a present, not a swapchain rebuild. */
    wl_surface_attach(popup->surface, nullptr, 0, 0);
    wl_surface_commit(popup->surface);

    popup->visible       = false;
    popup->selected      = -1;
    popup->present_retry = false;

    render_geometry_free(&popup->render, popup->geom);
    popup->geom = nullptr;
}

/* ── Core render ─────────────────────────────────────────────────────── */

static bool popup_render(TypioWlCandidatePopup *popup,
                          const TypioCandidateList *cands,
                          const char *preedit_text,
                          const char *status_text,
                          const char *mode_label) {
    const PopupConfig          *cfg;
    TypioCandidatePopupPalette   palette;
    uint64_t                     pal_sig;
    int                          scale;
    int                          new_selected;
    PopupDelta                   delta;
    uint64_t                     t0, t1;
    const char                  *delta_name = "unknown";
    static const TypioCandidateList empty_cands = {};

    if (!popup || !popup->surface) return false;
    if (!cands) {
        cands = &empty_cands;
    }

    popup->present_retry = false;

    t0  = typio_wl_monotonic_ms();
    cfg = get_config(popup);

    popup_config_build_palette(cfg, &popup->theme_cache, &palette);
    pal_sig      = typio_candidate_popup_palette_hash(&palette);
    scale        = render_scale(popup);
    new_selected = cands->count > 0 ? cands->selected : -1;

    delta = classify_delta(popup->geom, cands, preedit_text, status_text, mode_label,
                            cfg, pal_sig, scale, new_selected);

    if (delta == POPUP_DELTA_SELECTION && new_selected == popup->selected &&
        popup->visible) {
        return true;
    }

    /* Geometry recomputation may evict LRU layout entries and free the old
     * geometry — both release flux_image GPU resources. flux frees images
     * immediately and draw_image does not retain them, so any frame still in
     * flight that references those images would read freed GPU memory. Sync
     * the device before freeing. The selection-only hot path frees nothing and
     * deliberately skips this. */
    if (popup->fx_ready &&
        (delta == POPUP_DELTA_AUX || delta == POPUP_DELTA_CONTENT ||
         delta == POPUP_DELTA_STYLE)) {
        flux_device *dev = typio_flux_device_get();
        if (dev) flux_device_wait_idle(dev);
    }

    switch (delta) {
    case POPUP_DELTA_NONE:
        return true;

    case POPUP_DELTA_SELECTION:
        delta_name = "selection";
        break;  /* geometry unchanged; re-present with new selection */

    case POPUP_DELTA_AUX: {
        delta_name = "aux";
        PopupGeometry *new_geom = popup_geometry_update_aux(&popup->render,
                                                             popup->geom,
                                                             preedit_text,
                                                             mode_label);
        if (new_geom) {
            render_geometry_free(&popup->render, popup->geom);
            popup->geom = new_geom;
        } else {
            delta = POPUP_DELTA_CONTENT;  /* aux size changed; fall through */
        }
        break;
    }

    case POPUP_DELTA_STYLE:
        delta_name = "style";
        popup_render_ctx_invalidate(&popup->render);
        break;

    case POPUP_DELTA_CONTENT:
        delta_name = "content";
        break;
    }

    if (delta == POPUP_DELTA_CONTENT || delta == POPUP_DELTA_STYLE) {
        PopupGeometry *new_geom = popup_geometry_compute(&popup->render,
                                                          cands,
                                                          preedit_text,
                                                          status_text,
                                                          mode_label,
                                                          cfg, &palette, scale);
        if (!new_geom) {
            typio_log(TYPIO_LOG_WARNING, "Popup: geometry computation failed");
            return false;
        }
        render_geometry_free(&popup->render, popup->geom);
        popup->geom = new_geom;
    }

    if (!popup->geom) return false;

    int pw = popup->geom->popup_w * popup->geom->scale;
    int ph = popup->geom->popup_h * popup->geom->scale;
    if (!ensure_fx_surface(popup, pw, ph)) {
        typio_log(TYPIO_LOG_WARNING, "Popup: flux surface unavailable");
        return false;
    }

    /* The swapchain buffer is in physical pixels (logical × scale); tell the
     * compositor its scale so a HiDPI popup is shown at the correct logical
     * size and stays crisp. Pending state is applied by the WSI's commit at
     * present time. */
    wl_surface_set_buffer_scale(popup->surface, popup->geom->scale);

    PopupPresentResult pres = popup_present(popup, popup->geom, new_selected);
    bool ok = (pres == POPUP_PRESENT_OK);
    if (pres == POPUP_PRESENT_OK) {
        popup->selected = new_selected;
        popup->visible  = true;
    } else if (pres == POPUP_PRESENT_RETRY) {
        /* Compositor isn't releasing swapchain images yet (display asleep or
         * surface occluded after a lock/suspend). Skip this frame so the IME
         * event loop stays responsive, and ask it to re-present so the visible
         * highlight catches up once presentation resumes. selected/visible are
         * left unchanged so the retry re-renders this exact state. */
        popup->present_retry = true;
    } else {
        typio_log(TYPIO_LOG_WARNING, "Popup: present failed");
    }

    t1 = typio_wl_monotonic_ms();
    if (ok && (t1 - t0) >= POPUP_SLOW_RENDER_MS) {
        typio_log_debug("Popup slow render: %" PRIu64 "ms delta=%s candidates=%zu "
                        "selected=%d w=%d h=%d scale=%d sig=%" PRIu64,
                        t1 - t0, delta_name, cands->count, new_selected,
                        popup->geom ? popup->geom->popup_w : 0,
                        popup->geom ? popup->geom->popup_h : 0,
                        scale, cands->content_signature);
    }

    return ok;
}

/* ── Surface / output event handlers ───────────────────────────────── */

static void on_text_input_rectangle(void *data,
                                     [[maybe_unused]] struct zwp_input_popup_surface_v2 *s,
                                     int32_t x, int32_t y, int32_t w, int32_t h) {
    TypioWlCandidatePopup *popup = (TypioWlCandidatePopup *)data;
    popup->text_input_x = x;
    popup->text_input_y = y;
    popup->text_input_w = w;
    popup->text_input_h = h;
}

static const struct zwp_input_popup_surface_v2_listener popup_surface_listener = {
    .text_input_rectangle = on_text_input_rectangle,
};

static void on_surface_enter(void *data,
                               [[maybe_unused]] struct wl_surface *surface,
                               struct wl_output *output) {
    track_output((TypioWlCandidatePopup *)data, output);
}

static void on_surface_leave(void *data,
                               [[maybe_unused]] struct wl_surface *surface,
                               struct wl_output *output) {
    untrack_output((TypioWlCandidatePopup *)data, output);
}

static const struct wl_surface_listener wl_surface_listener = {
    .enter = on_surface_enter,
    .leave = on_surface_leave,
    .preferred_buffer_scale = nullptr,
    .preferred_buffer_transform = nullptr,
};

/* ── Output tracking (refresh popup when scale changes) ─────────────── */

static void refresh_visible(TypioWlCandidatePopup *popup) {
    if (!popup || !popup->visible || !popup->frontend || !popup->frontend->session) return;
    TypioInputContext *ctx = popup->frontend->session->ctx;
    if (!ctx) return;
    typio_wl_text_ui_backend_update(popup->frontend->text_ui_backend, ctx);
}

static void track_output(TypioWlCandidatePopup *popup, struct wl_output *output) {
    if (!popup || !output || tracks_output(popup, output)) return;
    PopupOutputRef *r = (PopupOutputRef *)calloc(1, sizeof(*r));
    if (!r) return;
    r->output = output;
    r->next = popup->entered_outputs;
    popup->entered_outputs = r;
    refresh_visible(popup);
}

static void untrack_output(TypioWlCandidatePopup *popup, struct wl_output *output) {
    PopupOutputRef **link = &popup->entered_outputs;
    while (*link) {
        PopupOutputRef *r = *link;
        if (r->output == output) {
            *link = r->next;
            free(r);
            refresh_visible(popup);
            return;
        }
        link = &r->next;
    }
}

static void clear_outputs(TypioWlCandidatePopup *popup) {
    while (popup && popup->entered_outputs) {
        PopupOutputRef *r = popup->entered_outputs;
        popup->entered_outputs = r->next;
        free(r);
    }
}

static bool ensure_created(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->text_ui_backend) return false;
    TypioWlTextUiBackend *backend = frontend->text_ui_backend;
    if (backend->candidate_popup) return backend->candidate_popup->surface && backend->candidate_popup->popup_surface;
    if (!frontend->compositor || !frontend->input_method) return false;
    backend->candidate_popup = typio_wl_candidate_popup_create(frontend);
    return backend->candidate_popup != nullptr;
}

/* ── Public API ─────────────────────────────────────────────────────── */

extern "C" TypioWlCandidatePopup *typio_wl_candidate_popup_create(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->compositor || !frontend->input_method) return nullptr;
    TypioWlCandidatePopup *popup = (TypioWlCandidatePopup *)calloc(1, sizeof(*popup));
    if (!popup) return nullptr;
    popup->frontend = frontend;
    popup->selected = -1;
    popup->vk_surface = VK_NULL_HANDLE;
    popup->surface = wl_compositor_create_surface(frontend->compositor);
    if (!popup->surface) { free(popup); return nullptr; }
    wl_surface_add_listener(popup->surface, &wl_surface_listener, popup);
    popup->popup_surface = zwp_input_method_v2_get_input_popup_surface(frontend->input_method, popup->surface);
    if (!popup->popup_surface) { wl_surface_destroy(popup->surface); free(popup); return nullptr; }
    zwp_input_popup_surface_v2_add_listener(popup->popup_surface, &popup_surface_listener, popup);
    popup_render_ctx_init(&popup->render);
    return popup;
}

extern "C" void typio_wl_candidate_popup_destroy(TypioWlCandidatePopup *popup) {
    if (!popup) return;
    hide_surface(popup);
    fx_teardown(popup);
    render_geometry_free(&popup->render, popup->geom);
    popup_render_ctx_free(&popup->render);
    clear_outputs(popup);
    free(popup->status_text);
    if (popup->popup_surface) zwp_input_popup_surface_v2_destroy(popup->popup_surface);
    if (popup->surface) wl_surface_destroy(popup->surface);
    free(popup);
}

extern "C" bool typio_wl_candidate_popup_update_content(TypioWlTextUiBackend *backend,
                                                             const TypioPanelContent *content) {
    if (!backend || !backend->frontend || !content) return false;
    if (!ensure_created(backend->frontend)) return false;
    TypioWlCandidatePopup *popup = backend->candidate_popup;
    if (!popup) return false;

    /* Update persistent status only when the caller explicitly sets it.
     * InputContext-driven updates leave status.message == nullptr so they
     * do not clobber a voice indicator that may still be visible. */
    if (content->status.active) {
        free(popup->status_text);
        popup->status_text = content->status.message ? strdup(content->status.message) : nullptr;
    } else if (content->status.message != nullptr) {
        /* Explicit clear request: hide_status passes active=false with an
         * empty-string message to distinguish "clear" from "don't care". */
        free(popup->status_text);
        popup->status_text = nullptr;
    }

    const TypioCandidateList *cands = content->input.candidates;
    const char *status = nullptr;

    /* No candidates and no persistent status → hide. */
    if ((!cands || cands->count == 0) && (!popup->status_text || !popup->status_text[0])) {
        hide_surface(popup);
        return true;
    }

    /* When there are no candidates, render the persistent status text
     * in its own zone with the status palette colour (Phase 3). */
    if (!cands || cands->count == 0) {
        status = popup->status_text;
    }

    char *mode_label = build_mode_label(popup);
    bool ok = popup_render(popup, cands, nullptr, status, mode_label);
    free(mode_label);
    return ok;
}

extern "C" bool typio_wl_candidate_popup_update(TypioWlTextUiBackend *backend, TypioInputContext *ctx) {
    if (!backend || !backend->frontend) return false;

    TypioPanelContent content;
    typio_panel_content_init(&content);
    if (ctx) {
        content.input.candidates = typio_input_context_get_candidates(ctx);
    }
    return typio_wl_candidate_popup_update_content(backend, &content);
}

extern "C" void typio_wl_candidate_popup_hide(TypioWlTextUiBackend *backend) {
    if (backend && backend->candidate_popup) hide_surface(backend->candidate_popup);
}

extern "C" bool typio_wl_candidate_popup_is_available(TypioWlTextUiBackend *backend) {
    return backend && backend->candidate_popup && backend->candidate_popup->surface && backend->candidate_popup->popup_surface;
}

extern "C" bool typio_wl_candidate_popup_present_retry_pending(TypioWlTextUiBackend *backend) {
    return backend && backend->candidate_popup && backend->candidate_popup->present_retry;
}

extern "C" void typio_wl_candidate_popup_invalidate_config(TypioWlTextUiBackend *backend) {
    if (!backend || !backend->candidate_popup) return;
    TypioWlCandidatePopup *popup = backend->candidate_popup;
    popup->config_valid = false;
    memset(&popup->theme_cache, 0, sizeof(popup->theme_cache));
    /* Frees all cached glyph images; sync first so an in-flight frame cannot
     * reference freed GPU resources (see popup_render). */
    if (popup->fx_ready) {
        flux_device *dev = typio_flux_device_get();
        if (dev) flux_device_wait_idle(dev);
    }
    popup_render_ctx_invalidate(&popup->render);
    render_geometry_free(&popup->render, popup->geom);
    popup->geom = nullptr;
}

extern "C" void typio_wl_candidate_popup_handle_output_change(TypioWlTextUiBackend *backend, struct wl_output *output) {
    if (!backend || !output || !backend->candidate_popup) return;
    TypioWlCandidatePopup *popup = backend->candidate_popup;
    if (!tracks_output(popup, output)) return;
    if (!find_frontend_output(popup, output)) untrack_output(popup, output);
    else refresh_visible(popup);
}

/* ── Status indicator (unified panel backend) ───────────────────────── */

extern "C" bool typio_wl_candidate_popup_show_status(TypioWlTextUiBackend *backend,
                                                      const char *text) {
    if (!backend || !backend->frontend) return false;

    TypioPanelContent content;
    typio_panel_content_init(&content);
    if (text && text[0]) {
        content.status.active  = true;
        content.status.message = text;
    }
    return typio_wl_candidate_popup_update_content(backend, &content);
}
