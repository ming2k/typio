/**
 * @file candidate_popup.c
 * @brief Wayland input-popup coordinator
 *
 * Coordinates three render paths for the candidate popup:
 * - selection-only: selected row changed, cached geometry unchanged
 * - aux-only: preedit/mode label changed, selected row unchanged, geometry unchanged
 * - full render: any content or geometry change, or any fast-path precondition failure
 */

#include "wl_frontend_internal.h"
#include "monotonic_time.h"
#include "candidate_popup_buffer.h"
#include "candidate_popup_layout.h"
#include "candidate_popup_paint.h"
#include "candidate_popup_render_state.h"
#include "candidate_popup_state.h"
#include "candidate_popup_theme.h"
#include "preedit_format.h"
#include "typio/config.h"
#include "typio/engine_label.h"
#include "typio/engine_manager.h"
#include "typio/instance.h"
#include "utils/log.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TYPIO_CANDIDATE_POPUP_DEFAULT_FONT_SIZE 11
#define TYPIO_CANDIDATE_POPUP_SLOW_RENDER_MS 8

typedef struct TypioWlCandidatePopupOutputRef {
    struct wl_output *output;
    struct TypioWlCandidatePopupOutputRef *next;
} TypioWlCandidatePopupOutputRef;

struct TypioWlCandidatePopup {
    TypioWlFrontend *frontend;
    struct wl_surface *surface;
    struct zwp_input_popup_surface_v2 *popup_surface;
    TypioCandidatePopupBuffer buffers[TYPIO_CANDIDATE_POPUP_BUFFER_COUNT];
    bool visible;
    int text_input_x;
    int text_input_y;
    int text_input_width;
    int text_input_height;
    TypioWlCandidatePopupOutputRef *entered_outputs;
    TypioCandidatePopupCache cache;
    TypioCandidatePopupFontCache font_cache;
    TypioCandidatePopupThemeCache theme_cache;
    TypioCandidatePopupConfigCache config_cache;
    TypioCandidatePopupBuffer *last_committed;
};

bool typio_wl_candidate_popup_update(TypioWlTextUiBackend *backend, TypioInputContext *ctx);
void typio_wl_candidate_popup_hide(TypioWlTextUiBackend *backend);
bool typio_wl_candidate_popup_is_available(TypioWlTextUiBackend *backend);
void typio_wl_candidate_popup_invalidate_config(TypioWlTextUiBackend *backend);
void typio_wl_candidate_popup_handle_output_change(TypioWlTextUiBackend *backend,
                                                   struct wl_output *output);

static void popup_handle_text_input_rectangle(void *data,
                                              struct zwp_input_popup_surface_v2 *popup_surface,
                                              int32_t x, int32_t y,
                                              int32_t width, int32_t height);
static void popup_handle_surface_enter(void *data, struct wl_surface *surface,
                                       struct wl_output *output);
static void popup_handle_surface_leave(void *data, struct wl_surface *surface,
                                       struct wl_output *output);

static const struct zwp_input_popup_surface_v2_listener popup_listener = {
    .text_input_rectangle = popup_handle_text_input_rectangle,
};

static const struct wl_surface_listener popup_surface_listener = {
    .enter = popup_handle_surface_enter,
    .leave = popup_handle_surface_leave,
};

static const TypioWlOutput *popup_find_frontend_output(const TypioWlCandidatePopup *popup,
                                                        struct wl_output *output) {
    for (TypioWlOutput *current = popup && popup->frontend ? popup->frontend->outputs : nullptr;
         current;
         current = current->next) {
        if (current->output == output) {
            return current;
        }
    }

    return nullptr;
}

static bool popup_tracks_output(const TypioWlCandidatePopup *popup, struct wl_output *output) {
    for (TypioWlCandidatePopupOutputRef *entry = popup ? popup->entered_outputs : nullptr;
         entry;
         entry = entry->next) {
        if (entry->output == output) {
            return true;
        }
    }

    return false;
}

static int popup_render_scale(const TypioWlCandidatePopup *popup) {
    int scale = 1;

    for (TypioWlCandidatePopupOutputRef *entry = popup ? popup->entered_outputs : nullptr;
         entry;
         entry = entry->next) {
        const TypioWlOutput *output = popup_find_frontend_output(popup, entry->output);
        if (output && output->scale > scale) {
            scale = output->scale;
        }
    }

    return scale;
}

static void popup_refresh_visible_surface(TypioWlCandidatePopup *popup) {
    TypioInputContext *ctx;

    if (!popup || !popup->visible || !popup->frontend || !popup->frontend->session) {
        return;
    }

    ctx = popup->frontend->session->ctx;
    if (!ctx) {
        return;
    }

    typio_wl_text_ui_backend_update(popup->frontend->text_ui_backend, ctx);
}

static void popup_track_output(TypioWlCandidatePopup *popup, struct wl_output *output) {
    TypioWlCandidatePopupOutputRef *entry;

    if (!popup || !output || popup_tracks_output(popup, output)) {
        return;
    }

    entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return;
    }

    entry->output = output;
    entry->next = popup->entered_outputs;
    popup->entered_outputs = entry;
    popup_refresh_visible_surface(popup);
}

static void popup_untrack_output(TypioWlCandidatePopup *popup, struct wl_output *output) {
    TypioWlCandidatePopupOutputRef **link;

    if (!popup || !output) {
        return;
    }

    link = &popup->entered_outputs;
    while (*link) {
        TypioWlCandidatePopupOutputRef *entry = *link;
        if (entry->output == output) {
            *link = entry->next;
            free(entry);
            popup_refresh_visible_surface(popup);
            return;
        }
        link = &entry->next;
    }
}

static void popup_clear_outputs(TypioWlCandidatePopup *popup) {
    while (popup && popup->entered_outputs) {
        TypioWlCandidatePopupOutputRef *entry = popup->entered_outputs;
        popup->entered_outputs = entry->next;
        free(entry);
    }
}

static void popup_load_render_config(TypioWlCandidatePopup *popup, TypioCandidatePopupRenderConfig *config) {
    const char *theme;
    const char *layout;
    TypioConfig *global_config;

    if (!popup || !config || !popup->frontend) {
        return;
    }

    config->theme_mode = TYPIO_CANDIDATE_POPUP_THEME_AUTO;
    config->layout_mode = TYPIO_CANDIDATE_POPUP_LAYOUT_VERTICAL;
    config->font_size = TYPIO_CANDIDATE_POPUP_DEFAULT_FONT_SIZE;
    config->mode_indicator = true;

    global_config = typio_instance_get_config(popup->frontend->instance);
    if (global_config) {
        theme = typio_config_get_string(global_config, "display.popup_theme", nullptr);
        layout = typio_config_get_string(global_config, "display.candidate_layout", nullptr);

        if (theme && strcmp(theme, "dark") == 0) {
            config->theme_mode = TYPIO_CANDIDATE_POPUP_THEME_DARK;
        } else if (theme && strcmp(theme, "light") == 0) {
            config->theme_mode = TYPIO_CANDIDATE_POPUP_THEME_LIGHT;
        }

        if (layout && strcmp(layout, "horizontal") == 0) {
            config->layout_mode = TYPIO_CANDIDATE_POPUP_LAYOUT_HORIZONTAL;
        } else if (layout && strcmp(layout, "vertical") == 0) {
            config->layout_mode = TYPIO_CANDIDATE_POPUP_LAYOUT_VERTICAL;
        }

        config->font_size = typio_config_get_int(global_config,
                                                 "display.font_size",
                                                 TYPIO_CANDIDATE_POPUP_DEFAULT_FONT_SIZE);
        config->mode_indicator = typio_config_get_bool(global_config,
                                                       "display.popup_mode_indicator",
                                                       true);
        if (config->font_size < 6) {
            config->font_size = 6;
        } else if (config->font_size > 72) {
            config->font_size = 72;
        }
    }

    snprintf(config->font_desc, sizeof(config->font_desc),
             "Sans %d", config->font_size);
    snprintf(config->page_font_desc, sizeof(config->page_font_desc),
             "Sans %d", config->font_size > 6 ? config->font_size - 1 : 6);
}

static const TypioCandidatePopupRenderConfig *popup_get_render_config(TypioWlCandidatePopup *popup) {
    TypioCandidatePopupConfigCache *cache;

    if (!popup) {
        return nullptr;
    }

    cache = &popup->config_cache;
    if (!cache->valid) {
        popup_load_render_config(popup, &cache->render_config);
        cache->valid = true;
    }

    return &cache->render_config;
}

static void popup_free_layout_result(TypioCandidatePopupLine *lines, size_t line_count,
                                     char *preedit_text,
                                     PangoLayout *preedit_layout,
                                     char *mode_label,
                                     PangoLayout *mode_label_layout) {
    TypioCandidatePopupCache transient_cache = {
        .lines = lines,
        .line_count = line_count,
        .preedit_text = preedit_text,
        .preedit_layout = preedit_layout,
        .mode_label = mode_label,
        .mode_label_layout = mode_label_layout,
    };

    typio_candidate_popup_layout_cache_invalidate(&transient_cache);
}

static char *popup_build_mode_label(TypioWlCandidatePopup *popup) {
    const TypioEngineMode *mode;
    TypioEngineManager *mgr;
    TypioEngine *active;
    const char *engine_name;
    const char *engine_label;
    char buf[128];

    if (!popup || !popup->frontend || !popup->frontend->instance) {
        return nullptr;
    }

    mode = typio_instance_get_last_mode(popup->frontend->instance);
    if (!mode || !mode->display_label || !mode->display_label[0]) {
        return nullptr;
    }

    mgr = typio_instance_get_engine_manager(popup->frontend->instance);
    active = mgr ? typio_engine_manager_get_active(mgr) : nullptr;
    engine_name = active ? typio_engine_get_name(active) : nullptr;
    engine_label = typio_engine_label_fallback(engine_name);

    if (engine_label && *engine_label) {
        snprintf(buf, sizeof(buf), "%s %s", engine_label, mode->display_label);
    } else {
        snprintf(buf, sizeof(buf), "%s", mode->display_label);
    }

    return strdup(buf);
}

static bool popup_render(TypioWlCandidatePopup *popup, const TypioPreedit *preedit,
                         const TypioCandidateList *candidates) {
    const TypioCandidatePopupRenderConfig *render_config;
    const TypioCandidatePopupPalette *palette;
    TypioCandidatePopupPaintTarget target;
    char *preedit_text = nullptr;
    char *mode_label = nullptr;
    PangoLayout *preedit_layout = nullptr;
    PangoLayout *mode_label_layout = nullptr;
    int preedit_width = 0;
    int preedit_height = 0;
    int preedit_x = 0;
    int preedit_y = 0;
    int mode_label_width = 0;
    int mode_label_height = 0;
    int mode_label_x = 0;
    int mode_label_y = 0;
    int mode_label_divider_y = -1;
    int width = 0;
    int height = 0;
    int scale;
    size_t line_count = 0;
    TypioCandidatePopupLine *lines = nullptr;
    uint64_t render_start_ms;
    uint64_t render_end_ms;
    TypioCandidatePopupRenderState cached_state;
    TypioCandidatePopupRenderState current_state;
    bool full_match;
    bool static_match;

    if (!popup || !popup->surface || !candidates || candidates->count == 0) {
        return false;
    }

    render_start_ms = typio_wl_monotonic_ms();

    scale = popup_render_scale(popup);
    render_config = popup_get_render_config(popup);
    if (!render_config) {
        return false;
    }

    if (preedit && preedit->segment_count > 0) {
        preedit_text = typio_wl_build_plain_preedit(preedit, nullptr);
    }

    if (render_config->mode_indicator) {
        mode_label = popup_build_mode_label(popup);
    }

    palette = typio_candidate_popup_theme_resolve(&popup->theme_cache, render_config->theme_mode);
    target = (TypioCandidatePopupPaintTarget){
        .surface = popup->surface,
        .shm = popup->frontend ? popup->frontend->shm : nullptr,
        .buffers = popup->buffers,
        .buffer_count = TYPIO_CANDIDATE_POPUP_BUFFER_COUNT,
    };

    cached_state = (TypioCandidatePopupRenderState){
        .cache_valid = popup->cache.valid,
        .line_count = popup->cache.line_count,
        .content_signature = popup->cache.content_signature,
        .palette_token = popup->cache.palette,
        .theme_mode = popup->cache.config.theme_mode,
        .layout_mode = popup->cache.config.layout_mode,
        .font_size = popup->cache.config.font_size,
        .font_desc = popup->cache.config.font_desc,
        .page_font_desc = popup->cache.config.page_font_desc,
        .width = popup->cache.width,
        .height = popup->cache.height,
        .preedit_text = popup->cache.preedit_text,
        .mode_label = popup->cache.mode_label,
    };
    current_state = (TypioCandidatePopupRenderState){
        .cache_valid = true,
        .line_count = candidates->count,
        .content_signature = candidates->content_signature,
        .palette_token = palette,
        .theme_mode = render_config->theme_mode,
        .layout_mode = render_config->layout_mode,
        .font_size = render_config->font_size,
        .font_desc = render_config->font_desc,
        .page_font_desc = render_config->page_font_desc,
        .width = popup->cache.width,
        .height = popup->cache.height,
        .preedit_text = preedit_text,
        .mode_label = mode_label,
    };
    full_match = typio_candidate_popup_render_state_matches(&cached_state, &current_state, scale);
    static_match = typio_candidate_popup_render_state_matches_static(&cached_state, &current_state,
                                                                     scale);

    if (full_match && popup->cache.selected != candidates->selected) {
        bool ok = false;
        TypioCandidatePopupBuffer *used_buffer = nullptr;

        if (popup->last_committed) {
            ok = typio_candidate_popup_paint_selection_update(&target,
                                                   popup->cache.lines,
                                                   popup->cache.line_count,
                                                   popup->cache.selected,
                                                   candidates->selected,
                                                   popup->cache.width,
                                                   popup->cache.height,
                                                   scale,
                                                   palette,
                                                   popup->last_committed,
                                                   &used_buffer);
        }

        if (!ok) {
            ok = typio_candidate_popup_paint_and_commit(&target,
                                                   popup->cache.lines,
                                                   popup->cache.line_count,
                                                   candidates->selected,
                                                   popup->cache.preedit_layout,
                                                   popup->cache.preedit_x,
                                                   popup->cache.preedit_y,
                                                   popup->cache.mode_label_layout,
                                                   popup->cache.mode_label_x,
                                                   popup->cache.mode_label_y,
                                                   popup->cache.mode_label_divider_y,
                                                   popup->cache.width,
                                                   popup->cache.height,
                                                   scale,
                                                   palette,
                                                   &used_buffer);
        }

        if (ok) {
            popup->cache.selected = candidates->selected;
            popup->last_committed = used_buffer;
            popup->visible = true;
        }

        free(preedit_text);
        free(mode_label);
        render_end_ms = typio_wl_monotonic_ms();
        if (ok && (render_end_ms - render_start_ms) >= TYPIO_CANDIDATE_POPUP_SLOW_RENDER_MS) {
            typio_log_debug(
                "Popup slow render: total=%" PRIu64 "ms fast_path=yes candidates=%zu selected=%d width=%d height=%d scale=%d signature=%" PRIu64,
                render_end_ms - render_start_ms,
                candidates->count,
                candidates->selected,
                popup->cache.width,
                popup->cache.height,
                scale,
                candidates->content_signature);
        }
        return ok;
    }

    if (static_match && popup->cache.selected == candidates->selected) {
        bool aux_changed = strcmp(popup->cache.preedit_text ? popup->cache.preedit_text : "",
                                  preedit_text ? preedit_text : "") != 0 ||
                           strcmp(popup->cache.mode_label ? popup->cache.mode_label : "",
                                  mode_label ? mode_label : "") != 0;
        if (aux_changed) {
            bool ok = false;
            TypioCandidatePopupBuffer *used_buffer = nullptr;

            if (!typio_candidate_popup_layout_measure_aux(&popup->cache,
                                                preedit_text, mode_label,
                                                render_config,
                                                &popup->font_cache,
                                                &preedit_layout,
                                                &preedit_width, &preedit_height,
                                                &preedit_x, &preedit_y,
                                                &mode_label_layout,
                                                &mode_label_width, &mode_label_height,
                                                &mode_label_x, &mode_label_y,
                                                &mode_label_divider_y,
                                                &width, &height)) {
                free(preedit_text);
                free(mode_label);
                return false;
            }

            if (width == popup->cache.width && height == popup->cache.height &&
                popup->last_committed) {
                ok = typio_candidate_popup_paint_aux_update(&target,
                                                  popup->cache.width,
                                                  popup->cache.height,
                                                  scale,
                                                  palette,
                                                  popup->last_committed,
                                                  popup->cache.preedit_layout,
                                                  popup->cache.preedit_x,
                                                  popup->cache.preedit_y,
                                                  popup->cache.preedit_width,
                                                  popup->cache.preedit_height,
                                                  preedit_layout,
                                                  preedit_x,
                                                  preedit_y,
                                                  preedit_width,
                                                  preedit_height,
                                                  popup->cache.mode_label_layout,
                                                  popup->cache.mode_label_x,
                                                  popup->cache.mode_label_y,
                                                  popup->cache.mode_label_width,
                                                  popup->cache.mode_label_height,
                                                  popup->cache.mode_label_divider_y,
                                                  mode_label_layout,
                                                  mode_label_x,
                                                  mode_label_y,
                                                  mode_label_width,
                                                  mode_label_height,
                                                  mode_label_divider_y,
                                                  &used_buffer);
            }

            if (ok) {
                popup->last_committed = used_buffer;
                popup->visible = true;
                typio_candidate_popup_layout_cache_update_aux(&popup->cache,
                                                     preedit_text,
                                                     preedit_layout,
                                                     preedit_width, preedit_height,
                                                     preedit_x, preedit_y,
                                                     mode_label,
                                                     mode_label_layout,
                                                     mode_label_width, mode_label_height,
                                                     mode_label_x, mode_label_y,
                                                     mode_label_divider_y);
                render_end_ms = typio_wl_monotonic_ms();
                if ((render_end_ms - render_start_ms) >= TYPIO_CANDIDATE_POPUP_SLOW_RENDER_MS) {
                    typio_log_debug(
                        "Popup slow render: total=%" PRIu64 "ms fast_path=aux candidates=%zu selected=%d width=%d height=%d scale=%d signature=%" PRIu64,
                        render_end_ms - render_start_ms,
                        candidates->count,
                        candidates->selected,
                        popup->cache.width,
                        popup->cache.height,
                        scale,
                        candidates->content_signature);
                }
                return true;
            }

            if (preedit_layout) {
                g_object_unref(preedit_layout);
                preedit_layout = nullptr;
            }
            if (mode_label_layout) {
                g_object_unref(mode_label_layout);
                mode_label_layout = nullptr;
            }
        }
    }

    if (!typio_candidate_popup_layout_compute(candidates, preedit_text, mode_label,
                                    render_config,
                                    &popup->font_cache, &lines, &line_count,
                                    &preedit_layout,
                                    &preedit_width, &preedit_height,
                                    &preedit_x, &preedit_y,
                                    &mode_label_layout,
                                    &mode_label_width, &mode_label_height,
                                    &mode_label_x, &mode_label_y,
                                    &mode_label_divider_y,
                                    &width, &height)) {
        free(preedit_text);
        free(mode_label);
        return false;
    }

    {
        TypioCandidatePopupBuffer *used_buffer = nullptr;
        if (!typio_candidate_popup_paint_and_commit(&target,
                                          lines, line_count,
                                          candidates->selected,
                                          preedit_layout,
                                          preedit_x,
                                          preedit_y,
                                          mode_label_layout,
                                          mode_label_x,
                                          mode_label_y,
                                          mode_label_divider_y,
                                          width, height, scale,
                                          palette,
                                          &used_buffer)) {
            popup_free_layout_result(lines, line_count, preedit_text,
                                     preedit_layout, mode_label, mode_label_layout);
            return false;
        }
        popup->last_committed = used_buffer;
    }

    popup->visible = true;
    typio_candidate_popup_layout_cache_store(&popup->cache, lines, line_count,
                                   candidates->selected,
                                   candidates->content_signature,
                                   preedit_text,
                                   preedit_layout,
                                   preedit_width, preedit_height,
                                   preedit_x, preedit_y,
                                   mode_label,
                                   mode_label_layout,
                                   mode_label_width, mode_label_height,
                                   mode_label_x, mode_label_y,
                                   mode_label_divider_y,
                                   width, height,
                                   render_config, palette);

    render_end_ms = typio_wl_monotonic_ms();
    if ((render_end_ms - render_start_ms) >= TYPIO_CANDIDATE_POPUP_SLOW_RENDER_MS) {
        typio_log_debug(
            "Popup slow render: total=%" PRIu64 "ms fast_path=no candidates=%zu selected=%d width=%d height=%d scale=%d signature=%" PRIu64,
            render_end_ms - render_start_ms,
            candidates->count,
            candidates->selected,
            width,
            height,
            scale,
            candidates->content_signature);
    }

    return true;
}

static void popup_handle_text_input_rectangle(void *data,
                                              [[maybe_unused]] struct zwp_input_popup_surface_v2 *popup_surface,
                                              int32_t x, int32_t y,
                                              int32_t width, int32_t height) {
    TypioWlCandidatePopup *popup = data;

    popup->text_input_x = x;
    popup->text_input_y = y;
    popup->text_input_width = width;
    popup->text_input_height = height;
}

static void popup_handle_surface_enter(void *data,
                                       [[maybe_unused]] struct wl_surface *surface,
                                       struct wl_output *output) {
    popup_track_output(data, output);
}

static void popup_handle_surface_leave(void *data,
                                       [[maybe_unused]] struct wl_surface *surface,
                                       struct wl_output *output) {
    popup_untrack_output(data, output);
}

static void popup_hide_surface(TypioWlCandidatePopup *popup) {
    if (!popup || !popup->surface || !popup->visible) {
        return;
    }

    wl_surface_attach(popup->surface, nullptr, 0, 0);
    wl_surface_commit(popup->surface);
    popup->visible = false;
    popup->last_committed = nullptr;
    typio_candidate_popup_layout_cache_invalidate(&popup->cache);
}

static bool popup_ensure_created(TypioWlFrontend *frontend) {
    TypioWlTextUiBackend *backend;

    if (!frontend) {
        return false;
    }

    backend = frontend->text_ui_backend;
    if (!backend) {
        return false;
    }

    if (backend->candidate_popup) {
        return backend->candidate_popup->surface && backend->candidate_popup->popup_surface;
    }

    if (!frontend->compositor || !frontend->shm || !frontend->input_method) {
        return false;
    }

    backend->candidate_popup = typio_wl_candidate_popup_create(frontend);
    if (!backend->candidate_popup) {
        typio_log(TYPIO_LOG_WARNING,
                  "Failed to create candidate popup surface on demand; using inline candidates");
        return false;
    }

    typio_log(TYPIO_LOG_DEBUG, "Created candidate popup surface on demand");
    return true;
}

TypioWlCandidatePopup *typio_wl_candidate_popup_create(TypioWlFrontend *frontend) {
    TypioWlCandidatePopup *popup;

    if (!frontend || !frontend->compositor || !frontend->shm || !frontend->input_method) {
        return nullptr;
    }

    popup = calloc(1, sizeof(*popup));
    if (!popup) {
        return nullptr;
    }

    popup->frontend = frontend;
    popup->surface = wl_compositor_create_surface(frontend->compositor);
    if (!popup->surface) {
        free(popup);
        return nullptr;
    }
    wl_surface_add_listener(popup->surface, &popup_surface_listener, popup);

    popup->popup_surface = zwp_input_method_v2_get_input_popup_surface(
        frontend->input_method, popup->surface);
    if (!popup->popup_surface) {
        wl_surface_destroy(popup->surface);
        free(popup);
        return nullptr;
    }

    zwp_input_popup_surface_v2_add_listener(popup->popup_surface,
                                            &popup_listener, popup);
    typio_log(TYPIO_LOG_DEBUG, "Input popup surface initialized");
    return popup;
}

void typio_wl_candidate_popup_destroy(TypioWlCandidatePopup *popup) {
    if (!popup) {
        return;
    }

    popup_hide_surface(popup);
    popup->last_committed = nullptr;
    typio_candidate_popup_layout_cache_invalidate(&popup->cache);
    for (size_t i = 0; i < TYPIO_CANDIDATE_POPUP_BUFFER_COUNT; ++i) {
        typio_candidate_popup_buffer_reset(&popup->buffers[i]);
    }
    popup_clear_outputs(popup);
    typio_candidate_popup_font_cache_free(&popup->font_cache);

    if (popup->popup_surface) {
        zwp_input_popup_surface_v2_destroy(popup->popup_surface);
    }
    if (popup->surface) {
        wl_surface_destroy(popup->surface);
    }

    free(popup);
}

void typio_wl_candidate_popup_invalidate_config(TypioWlTextUiBackend *backend) {
    TypioWlCandidatePopup *popup;
    TypioCandidatePopupInvalidationState state;

    if (!backend || !backend->candidate_popup) {
        return;
    }

    popup = backend->candidate_popup;
    state.config_cache_valid = popup->config_cache.valid;
    state.theme_cache_valid = popup->theme_cache.palette != nullptr;
    state.render_cache_valid = popup->cache.valid;
    typio_candidate_popup_state_invalidate_config(&state);

    popup->config_cache.valid = state.config_cache_valid;
    if (!state.theme_cache_valid) {
        memset(&popup->theme_cache, 0, sizeof(popup->theme_cache));
    }
    if (!state.render_cache_valid) {
        popup->last_committed = nullptr;
        typio_candidate_popup_layout_cache_invalidate(&popup->cache);
    }
}

bool typio_wl_candidate_popup_update(TypioWlTextUiBackend *backend, TypioInputContext *ctx) {
    const TypioCandidateList *candidates;
    TypioWlFrontend *frontend;

    if (!backend || !backend->frontend || !ctx) {
        return false;
    }

    frontend = backend->frontend;
    if (!popup_ensure_created(frontend)) {
        return false;
    }

    candidates = typio_input_context_get_candidates(ctx);
    if (!candidates || candidates->count == 0) {
        popup_hide_surface(backend->candidate_popup);
        return true;
    }

    if (!popup_render(backend->candidate_popup, nullptr, candidates)) {
        typio_log(TYPIO_LOG_WARNING,
                  "Popup render failed; keeping previous popup frame");
        return false;
    }

    typio_log(TYPIO_LOG_DEBUG, "Rendered candidate popup (%zu items)",
              candidates->count);
    return true;
}

void typio_wl_candidate_popup_hide(TypioWlTextUiBackend *backend) {
    if (!backend || !backend->candidate_popup) {
        return;
    }

    popup_hide_surface(backend->candidate_popup);
}

bool typio_wl_candidate_popup_is_available(TypioWlTextUiBackend *backend) {
    return backend && backend->candidate_popup && backend->candidate_popup->surface &&
           backend->candidate_popup->popup_surface;
}

void typio_wl_candidate_popup_handle_output_change(TypioWlTextUiBackend *backend,
                                                   struct wl_output *output) {
    TypioWlCandidatePopup *popup;
    TypioCandidatePopupOutputChangeAction action;

    if (!backend || !output) {
        return;
    }

    popup = backend->candidate_popup;
    action = typio_candidate_popup_state_handle_output_change(popup != nullptr,
                                                              popup && popup_tracks_output(popup, output),
                                                              popup && popup_find_frontend_output(popup, output));
    switch (action) {
        case TYPIO_CANDIDATE_POPUP_OUTPUT_CHANGE_UNTRACK:
            popup_untrack_output(popup, output);
            return;
        case TYPIO_CANDIDATE_POPUP_OUTPUT_CHANGE_REFRESH:
            popup_refresh_visible_surface(popup);
            return;
        case TYPIO_CANDIDATE_POPUP_OUTPUT_CHANGE_IGNORE:
        default:
            return;
    }
}
