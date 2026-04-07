/**
 * @file candidate_popup.c
 * @brief Wayland input-popup coordinator (redesigned).
 *
 * Architecture
 * ============
 *
 *  PopupDelta (enum)
 *    Classify every update into exactly one of:
 *      NONE       — nothing visible changed, skip rendering
 *      SELECTION  — only selected index changed; repaint two rows
 *      AUX        — only preedit/mode label changed; repaint aux area
 *      CONTENT    — candidate list changed (page navigation); full render
 *      STYLE      — font/theme/scale changed; full render + cache invalidation
 *
 *  PopupGeometry (immutable snapshot)
 *    Computed by popup_geometry_compute() in candidate_popup_layout.c.
 *    Does NOT contain `selected` — selection changes are rendered by
 *    referencing the geometry without recomputing it.
 *
 *  PopupPangoCtx (persistent, per-popup)
 *    A PangoContext + 64-entry LRU layout cache.  No scratch Cairo surfaces
 *    for measurement.  Candidate layouts survive page changes if the same
 *    text reappears.
 *
 *  Render paths
 *    SELECTION → popup_paint_selection  (2-row blit + repaint, ~1 ms)
 *    AUX       → popup_paint_aux        (aux-area blit + repaint)
 *    CONTENT   → popup_paint_full       (full repaint; fast with warm cache)
 *    STYLE     → cache invalidation + popup_paint_full
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

    /* Per-popup Pango context + LRU layout cache */
    PopupPangoCtx pango;

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
    POPUP_DELTA_NONE,       /* nothing changed                              */
    POPUP_DELTA_SELECTION,  /* only selected index changed                  */
    POPUP_DELTA_AUX,        /* only preedit / mode label changed            */
    POPUP_DELTA_CONTENT,    /* candidate list content changed (page change) */
    POPUP_DELTA_STYLE,      /* font, theme, or scale changed                */
} PopupDelta;

static PopupDelta classify_delta(const PopupGeometry *geom,
                                  const TypioCandidateList *cands,
                                  const char *preedit,
                                  const char *mode_label,
                                  const PopupConfig *cfg,
                                  const TypioCandidatePopupPalette *palette,
                                  int scale,
                                  int new_selected) {
    if (!geom) {
        return POPUP_DELTA_STYLE;  /* no prior geometry — treat as full rebuild */
    }

    /* Style changes invalidate everything */
    if (geom->scale != scale ||
        geom->palette != palette ||
        geom->config.theme_mode   != cfg->theme_mode   ||
        geom->config.layout_mode  != cfg->layout_mode  ||
        geom->config.font_size    != cfg->font_size     ||
        geom->config.mode_indicator != cfg->mode_indicator ||
        strcmp(geom->config.font_desc,     cfg->font_desc)     != 0 ||
        strcmp(geom->config.aux_font_desc, cfg->aux_font_desc) != 0) {
        return POPUP_DELTA_STYLE;
    }

    /* Content change: new page or different candidates */
    if (geom->content_sig != cands->content_signature ||
        geom->row_count   != cands->count) {
        return POPUP_DELTA_CONTENT;
    }

    /* Aux change: same candidates, different preedit or mode label */
    const char *cur_pre  = preedit    ? preedit    : "";
    const char *cur_mode = mode_label ? mode_label : "";
    bool preedit_same = strcmp(geom->preedit_text, cur_pre)  == 0;
    bool mode_same    = strcmp(geom->mode_label,   cur_mode) == 0;

    if (!preedit_same || !mode_same) {
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

/* ── Surface hide ───────────────────────────────────────────────────── */

static void hide_surface(TypioWlCandidatePopup *popup) {
    if (!popup || !popup->surface || !popup->visible) return;

    wl_surface_attach(popup->surface, nullptr, 0, 0);
    wl_surface_commit(popup->surface);

    popup->visible       = false;
    popup->last_committed = nullptr;
    popup->selected      = -1;

    popup_geometry_free(popup->geom);
    popup->geom = nullptr;
}

/* ── Core render ─────────────────────────────────────────────────────── */

static bool popup_render(TypioWlCandidatePopup *popup,
                          const TypioCandidateList *cands,
                          const char *preedit_text,
                          const char *mode_label) {
    const PopupConfig              *cfg;
    const TypioCandidatePopupPalette *palette;
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

    palette = typio_candidate_popup_theme_resolve(&popup->theme_cache,
                                                   cfg->theme_mode);
    scale        = render_scale(popup);
    new_selected = cands->selected;

    target = (PopupPaintTarget){
        .surface      = popup->surface,
        .shm          = popup->frontend ? popup->frontend->shm : nullptr,
        .buffers      = popup->buffers,
        .buffer_count = TYPIO_CANDIDATE_POPUP_BUFFER_COUNT,
    };

    delta = classify_delta(popup->geom, cands, preedit_text, mode_label,
                            cfg, palette, scale, new_selected);

    /* ── NONE: selection identical to what's already on screen ───────── */
    if (delta == POPUP_DELTA_SELECTION && new_selected == popup->selected) {
        delta = POPUP_DELTA_NONE;
    }

    switch (delta) {

    case POPUP_DELTA_NONE:
        return true;

    /* ── SELECTION: repaint two rows ─────────────────────────────────── */
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
        if (!ok) {
            /* Fallback: full repaint */
            delta_name = "selection→full";
            goto full_render;
        }
        break;

    /* ── AUX: repaint preedit / mode label only ──────────────────────── */
    case POPUP_DELTA_AUX: {
        delta_name = "aux";
        PopupGeometry *new_geom = popup_geometry_update_aux(&popup->pango,
                                                             popup->geom,
                                                             preedit_text,
                                                             mode_label);
        if (new_geom && popup->last_committed) {
            TypioCandidatePopupBuffer *used = nullptr;
            ok = popup_paint_aux(&target, popup->geom, new_geom,
                                  popup->last_committed, &used);
            if (ok) {
                popup_geometry_free(popup->geom);
                popup->geom           = new_geom;
                popup->last_committed = used;
                popup->visible        = true;
            } else {
                popup_geometry_free(new_geom);
            }
        } else {
            popup_geometry_free(new_geom);
        }
        if (!ok) {
            delta_name = "aux→full";
            goto full_render;
        }
        break;
    }

    /* ── CONTENT / STYLE / fallback: full render ─────────────────────── */
    case POPUP_DELTA_STYLE:
        delta_name = "style";
        popup_pango_ctx_invalidate(&popup->pango);
        /* fall through */
    case POPUP_DELTA_CONTENT:
        if (delta == POPUP_DELTA_CONTENT) delta_name = "content";
        /* fall through */
    full_render: {
        PopupGeometry *new_geom = popup_geometry_compute(&popup->pango,
                                                          cands,
                                                          preedit_text,
                                                          mode_label,
                                                          cfg, palette, scale);
        if (!new_geom) {
            typio_log(TYPIO_LOG_WARNING,
                      "Popup: geometry computation failed");
            return false;
        }

        TypioCandidatePopupBuffer *used = nullptr;
        ok = popup_paint_full(&target, new_geom, new_selected, &used);
        if (ok) {
            popup_geometry_free(popup->geom);
            popup->geom           = new_geom;
            popup->selected       = new_selected;
            popup->last_committed = used;
            popup->visible        = true;
        } else {
            popup_geometry_free(new_geom);
            typio_log(TYPIO_LOG_WARNING,
                      "Popup: paint_full failed; keeping previous frame");
        }
        break;
    }

    } /* switch */

    t1 = typio_wl_monotonic_ms();
    if (ok && (t1 - t0) >= POPUP_SLOW_RENDER_MS) {
        typio_log_debug(
            "Popup slow render: %" PRIu64 "ms delta=%s candidates=%zu "
            "selected=%d w=%d h=%d scale=%d sig=%" PRIu64,
            t1 - t0, delta_name,
            cands->count, new_selected,
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
    TypioWlCandidatePopup *popup = data;
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
    track_output(data, output);
}

static void on_surface_leave(void *data,
                               [[maybe_unused]] struct wl_surface *surface,
                               struct wl_output *output) {
    untrack_output(data, output);
}

static const struct wl_surface_listener wl_surface_listener = {
    .enter = on_surface_enter,
    .leave = on_surface_leave,
};

/* ── Output tracking (refresh popup when scale changes) ─────────────── */

static void refresh_visible(TypioWlCandidatePopup *popup) {
    TypioInputContext *ctx;

    if (!popup || !popup->visible || !popup->frontend ||
        !popup->frontend->session) return;

    ctx = popup->frontend->session->ctx;
    if (!ctx) return;

    typio_wl_text_ui_backend_update(popup->frontend->text_ui_backend, ctx);
}

static void track_output(TypioWlCandidatePopup *popup, struct wl_output *output) {
    PopupOutputRef *r;

    if (!popup || !output || tracks_output(popup, output)) return;

    r = calloc(1, sizeof(*r));
    if (!r) return;

    r->output          = output;
    r->next            = popup->entered_outputs;
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

/* ── Ensure popup surface is created on demand ──────────────────────── */

static bool ensure_created(TypioWlFrontend *frontend) {
    TypioWlTextUiBackend *backend;

    if (!frontend) return false;

    backend = frontend->text_ui_backend;
    if (!backend) return false;

    if (backend->candidate_popup) {
        return backend->candidate_popup->surface &&
               backend->candidate_popup->popup_surface;
    }

    if (!frontend->compositor || !frontend->shm || !frontend->input_method) {
        return false;
    }

    backend->candidate_popup = typio_wl_candidate_popup_create(frontend);
    if (!backend->candidate_popup) {
        typio_log(TYPIO_LOG_WARNING,
                  "Popup: failed to create popup surface; using inline candidates");
        return false;
    }

    typio_log(TYPIO_LOG_DEBUG, "Popup: created surface on demand");
    return true;
}

/* ── Public API ─────────────────────────────────────────────────────── */

TypioWlCandidatePopup *typio_wl_candidate_popup_create(TypioWlFrontend *frontend) {
    TypioWlCandidatePopup *popup;

    if (!frontend || !frontend->compositor ||
        !frontend->shm || !frontend->input_method) return nullptr;

    popup = calloc(1, sizeof(*popup));
    if (!popup) return nullptr;

    popup->frontend  = frontend;
    popup->selected  = -1;

    popup->surface = wl_compositor_create_surface(frontend->compositor);
    if (!popup->surface) {
        free(popup);
        return nullptr;
    }
    wl_surface_add_listener(popup->surface, &wl_surface_listener, popup);

    popup->popup_surface = zwp_input_method_v2_get_input_popup_surface(
        frontend->input_method, popup->surface);
    if (!popup->popup_surface) {
        wl_surface_destroy(popup->surface);
        free(popup);
        return nullptr;
    }
    zwp_input_popup_surface_v2_add_listener(popup->popup_surface,
                                             &popup_surface_listener, popup);

    popup_pango_ctx_init(&popup->pango);

    typio_log(TYPIO_LOG_DEBUG, "Popup: surface initialised");
    return popup;
}

void typio_wl_candidate_popup_destroy(TypioWlCandidatePopup *popup) {
    size_t i;

    if (!popup) return;

    hide_surface(popup);

    popup_geometry_free(popup->geom);
    popup->geom = nullptr;

    popup_pango_ctx_free(&popup->pango);

    for (i = 0; i < TYPIO_CANDIDATE_POPUP_BUFFER_COUNT; ++i) {
        typio_candidate_popup_buffer_reset(&popup->buffers[i]);
    }

    clear_outputs(popup);

    if (popup->popup_surface) {
        zwp_input_popup_surface_v2_destroy(popup->popup_surface);
    }
    if (popup->surface) {
        wl_surface_destroy(popup->surface);
    }

    free(popup);
}

bool typio_wl_candidate_popup_update(TypioWlTextUiBackend *backend,
                                      TypioInputContext *ctx) {
    const TypioCandidateList *cands;
    TypioWlFrontend          *frontend;
    TypioWlCandidatePopup    *popup;
    char                     *mode_label;
    bool                      ok;

    if (!backend || !backend->frontend || !ctx) return false;

    frontend = backend->frontend;
    if (!ensure_created(frontend)) return false;

    popup = backend->candidate_popup;
    cands = typio_input_context_get_candidates(ctx);

    if (!cands || cands->count == 0) {
        hide_surface(popup);
        return true;
    }

    mode_label = build_mode_label(popup);
    ok = popup_render(popup, cands, nullptr, mode_label);
    free(mode_label);

    if (!ok) {
        typio_log(TYPIO_LOG_WARNING,
                  "Popup: render failed; keeping previous frame");
        return false;
    }

    typio_log(TYPIO_LOG_DEBUG, "Popup: rendered %zu candidates", cands->count);
    return true;
}

void typio_wl_candidate_popup_hide(TypioWlTextUiBackend *backend) {
    if (!backend || !backend->candidate_popup) return;
    hide_surface(backend->candidate_popup);
}

bool typio_wl_candidate_popup_is_available(TypioWlTextUiBackend *backend) {
    return backend && backend->candidate_popup &&
           backend->candidate_popup->surface &&
           backend->candidate_popup->popup_surface;
}

void typio_wl_candidate_popup_invalidate_config(TypioWlTextUiBackend *backend) {
    TypioWlCandidatePopup *popup;

    if (!backend || !backend->candidate_popup) return;

    popup = backend->candidate_popup;
    popup->config_valid = false;
    memset(&popup->theme_cache, 0, sizeof(popup->theme_cache));

    /* Invalidate the layout cache and geometry so the next render rebuilds */
    popup_pango_ctx_invalidate(&popup->pango);
    popup_geometry_free(popup->geom);
    popup->geom           = nullptr;
    popup->last_committed = nullptr;
}

void typio_wl_candidate_popup_handle_output_change(TypioWlTextUiBackend *backend,
                                                    struct wl_output *output) {
    TypioWlCandidatePopup *popup;

    if (!backend || !output) return;

    popup = backend->candidate_popup;
    if (!popup) return;

    if (!tracks_output(popup, output)) return;

    if (!find_frontend_output(popup, output)) {
        /* Output removed from frontend — stop tracking */
        untrack_output(popup, output);
    } else {
        /* Output scale may have changed — refresh */
        refresh_visible(popup);
    }
}
