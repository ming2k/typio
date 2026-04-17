/**
 * @file candidate_popup.cc
 * @brief Wayland input-popup coordinator (Skia version).
 */

#include "wl_frontend_internal.h"
#include "candidate_popup_layout.h"
#include "candidate_popup_paint.h"
#include "candidate_popup_buffer.h"
#include "candidate_popup_theme.h"
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

    /* SHM triple-buffer pool */
    TypioCandidatePopupBuffer  buffers[TYPIO_CANDIDATE_POPUP_BUFFER_COUNT];
    TypioCandidatePopupBuffer *last_committed;

    /* Per-popup Skia engine context + LRU layout cache */
    PopupSkiaCtx skia;

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
                                  const char *mode_label,
                                  const PopupConfig *cfg,
                                  uint64_t palette_sig,
                                  int scale,
                                  int new_selected) {
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

        /* 
         * Optimization for Rime dynamic comments: if only the newly selected 
         * candidate has different text/comment/label, and everything else matches 
         * the current geometry, we can treat it as a selection change that also 
         * needs a content refresh for the new row.
         *
         * Note: this assumes we can trust the caller's 'new_selected' index.
         */
        if (new_selected >= 0 && (size_t)new_selected < cands->count) {
            bool others_match = true;
            for (size_t i = 0; i < cands->count; ++i) {
                if ((int)i == new_selected) continue;
                
                /* This is a bit expensive but still much cheaper than a full Skia layout/render cycle 
                 * for the whole window. We use the content_signature of individual rows if we had them,
                 * but for now we just do nothing and fall back to full content update if we are unsure.
                 */
                 others_match = false; 
                 break;
            }
            
            /* Actually, without per-row signatures in the core API, we can't easily 
             * prove only one row changed here without a lot of string work. 
             * Let's stick to the safe path for now but mark it for future core improvement. 
             */
        }

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

static void skia_geometry_free(PopupSkiaCtx *pc, PopupGeometry *g) {
    if (!g) return;
    if (g->preedit_layout) pc->engine->vtable->free_layout(g->preedit_layout);
    if (g->mode_layout) pc->engine->vtable->free_layout(g->mode_layout);
    popup_geometry_free(g);
}

/* ── Surface hide ───────────────────────────────────────────────────── */

static void hide_surface(TypioWlCandidatePopup *popup) {
    if (!popup || !popup->surface || !popup->visible) return;

    wl_surface_attach(popup->surface, nullptr, 0, 0);
    wl_surface_commit(popup->surface);

    popup->visible       = false;
    popup->last_committed = nullptr;
    popup->selected      = -1;

    skia_geometry_free(&popup->skia, popup->geom);
    popup->geom = nullptr;
}

/* ── Core render ─────────────────────────────────────────────────────── */

static bool popup_render(TypioWlCandidatePopup *popup,
                          const TypioCandidateList *cands,
                          const char *preedit_text,
                          const char *mode_label) {
    const PopupConfig              *cfg;
    TypioCandidatePopupPalette       palette;
    uint64_t                         pal_sig;
    PopupPaintTarget                 target;
    int                              scale;
    int                              new_selected;
    PopupDelta                       delta;
    uint64_t                         t0, t1;
    const char                      *delta_name = "unknown";
    bool                             ok          = false;

    if (!popup || !popup->surface || !cands) return false;

    t0  = typio_wl_monotonic_ms();
    cfg = get_config(popup);

    popup_config_build_palette(cfg, &popup->theme_cache, &palette);
    pal_sig      = typio_candidate_popup_palette_hash(&palette);
    scale        = render_scale(popup);
    new_selected = cands->selected;

    target = (PopupPaintTarget){
        .surface      = popup->surface,
        .shm          = popup->frontend ? popup->frontend->shm : nullptr,
        .buffers      = popup->buffers,
        .buffer_count = TYPIO_CANDIDATE_POPUP_BUFFER_COUNT,
    };

    delta = classify_delta(popup->geom, cands, preedit_text, mode_label,
                            cfg, pal_sig, scale, new_selected);

    if (delta == POPUP_DELTA_SELECTION && new_selected == popup->selected) {
        delta = POPUP_DELTA_NONE;
    }

    bool force_full_render = false;

    switch (delta) {
    case POPUP_DELTA_NONE:
        return true;

    case POPUP_DELTA_SELECTION:
        delta_name = "selection";
        if (popup->last_committed) {
            TypioCandidatePopupBuffer *used = nullptr;
            ok = popup_paint_selection(&target, popup->geom,
                                        popup->selected, new_selected,
                                        popup->last_committed, &used);
            if (ok) {
                popup->selected       = new_selected;
                popup->last_committed = used;
            }
        }
        if (!ok) force_full_render = true;
        break;

    case POPUP_DELTA_AUX: {
        delta_name = "aux";
        PopupGeometry *new_geom = popup_geometry_update_aux(&popup->skia,
                                                             popup->geom,
                                                             preedit_text,
                                                             mode_label);
        if (new_geom && popup->last_committed) {
            TypioCandidatePopupBuffer *used = nullptr;
            ok = popup_paint_aux(&target, popup->geom, new_geom,
                                  popup->last_committed, &used);
            if (ok) {
                skia_geometry_free(&popup->skia, popup->geom);
                popup->geom           = new_geom;
                popup->last_committed = used;
                popup->visible        = true;
            } else {
                skia_geometry_free(&popup->skia, new_geom);
            }
        } else {
            skia_geometry_free(&popup->skia, new_geom);
        }
        if (!ok) force_full_render = true;
        break;
    }

    case POPUP_DELTA_STYLE:
        delta_name = "style";
        popup_skia_ctx_invalidate(&popup->skia);
        force_full_render = true;
        break;
    case POPUP_DELTA_CONTENT:
        delta_name = "content";
        force_full_render = true;
        break;
    }

    if (force_full_render) {
        PopupGeometry *new_geom = popup_geometry_compute(&popup->skia,
                                                          cands,
                                                          preedit_text,
                                                          mode_label,
                                                          cfg, &palette, scale);
        if (!new_geom) {
            typio_log(TYPIO_LOG_WARNING, "Popup: geometry computation failed");
            return false;
        }

        TypioCandidatePopupBuffer *used = nullptr;
        ok = popup_paint_full(&target, new_geom, new_selected, &used);
        if (ok) {
            skia_geometry_free(&popup->skia, popup->geom);
            popup->geom           = new_geom;
            popup->selected       = new_selected;
            popup->last_committed = used;
            popup->visible        = true;
        } else {
            skia_geometry_free(&popup->skia, new_geom);
            typio_log(TYPIO_LOG_WARNING, "Popup: paint_full failed");
        }
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
    if (!frontend->compositor || !frontend->shm || !frontend->input_method) return false;
    backend->candidate_popup = typio_wl_candidate_popup_create(frontend);
    return backend->candidate_popup != nullptr;
}

/* ── Public API ─────────────────────────────────────────────────────── */

extern "C" TypioWlCandidatePopup *typio_wl_candidate_popup_create(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->compositor || !frontend->shm || !frontend->input_method) return nullptr;
    TypioWlCandidatePopup *popup = (TypioWlCandidatePopup *)calloc(1, sizeof(*popup));
    if (!popup) return nullptr;
    popup->frontend = frontend;
    popup->selected = -1;
    popup->surface = wl_compositor_create_surface(frontend->compositor);
    if (!popup->surface) { free(popup); return nullptr; }
    wl_surface_add_listener(popup->surface, &wl_surface_listener, popup);
    popup->popup_surface = zwp_input_method_v2_get_input_popup_surface(frontend->input_method, popup->surface);
    if (!popup->popup_surface) { wl_surface_destroy(popup->surface); free(popup); return nullptr; }
    zwp_input_popup_surface_v2_add_listener(popup->popup_surface, &popup_surface_listener, popup);
    popup_skia_ctx_init(&popup->skia);
    return popup;
}

extern "C" void typio_wl_candidate_popup_destroy(TypioWlCandidatePopup *popup) {
    if (!popup) return;
    hide_surface(popup);
    skia_geometry_free(&popup->skia, popup->geom);
    popup_skia_ctx_free(&popup->skia);
    for (size_t i = 0; i < TYPIO_CANDIDATE_POPUP_BUFFER_COUNT; ++i) typio_candidate_popup_buffer_reset(&popup->buffers[i]);
    clear_outputs(popup);
    if (popup->popup_surface) zwp_input_popup_surface_v2_destroy(popup->popup_surface);
    if (popup->surface) wl_surface_destroy(popup->surface);
    free(popup);
}

extern "C" bool typio_wl_candidate_popup_update(TypioWlTextUiBackend *backend, TypioInputContext *ctx) {
    if (!backend || !backend->frontend || !ctx) return false;
    if (!ensure_created(backend->frontend)) return false;
    TypioWlCandidatePopup *popup = backend->candidate_popup;
    const TypioCandidateList *cands = typio_input_context_get_candidates(ctx);
    if (!cands || cands->count == 0) { hide_surface(popup); return true; }
    char *mode_label = build_mode_label(popup);
    bool ok = popup_render(popup, cands, nullptr, mode_label);
    free(mode_label);
    return ok;
}

extern "C" void typio_wl_candidate_popup_hide(TypioWlTextUiBackend *backend) {
    if (backend && backend->candidate_popup) hide_surface(backend->candidate_popup);
}

extern "C" bool typio_wl_candidate_popup_is_available(TypioWlTextUiBackend *backend) {
    return backend && backend->candidate_popup && backend->candidate_popup->surface && backend->candidate_popup->popup_surface;
}

extern "C" void typio_wl_candidate_popup_invalidate_config(TypioWlTextUiBackend *backend) {
    if (!backend || !backend->candidate_popup) return;
    TypioWlCandidatePopup *popup = backend->candidate_popup;
    popup->config_valid = false;
    memset(&popup->theme_cache, 0, sizeof(popup->theme_cache));
    popup_skia_ctx_invalidate(&popup->skia);
    skia_geometry_free(&popup->skia, popup->geom);
    popup->geom = nullptr;
    popup->last_committed = nullptr;
}

extern "C" void typio_wl_candidate_popup_handle_output_change(TypioWlTextUiBackend *backend, struct wl_output *output) {
    if (!backend || !output || !backend->candidate_popup) return;
    TypioWlCandidatePopup *popup = backend->candidate_popup;
    if (!tracks_output(popup, output)) return;
    if (!find_frontend_output(popup, output)) untrack_output(popup, output);
    else refresh_visible(popup);
}
