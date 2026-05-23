#ifndef TYPIO_WL_TEXT_UI_BACKEND_H
#define TYPIO_WL_TEXT_UI_BACKEND_H

#include "panel_content.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wl_output;

typedef struct TypioInputContext TypioInputContext;
typedef struct TypioWlFrontend TypioWlFrontend;
typedef struct TypioWlTextUiBackend TypioWlTextUiBackend;

TypioWlTextUiBackend *typio_wl_text_ui_backend_create(TypioWlFrontend *frontend);
void typio_wl_text_ui_backend_destroy(TypioWlTextUiBackend *backend);

bool typio_wl_text_ui_backend_update(TypioWlTextUiBackend *backend,
                                     TypioInputContext *ctx);
void typio_wl_text_ui_backend_hide(TypioWlTextUiBackend *backend);
bool typio_wl_text_ui_backend_is_available(TypioWlTextUiBackend *backend);

/* Unified content update (Phase 2 of unified panel backend).
 * Accepts a TypioPanelContent that aggregates data from InputContext,
 * VoiceService, and other subsystems.  This is the primary update path;
 * typio_wl_text_ui_backend_update() is a convenience wrapper for the
 * InputContext-only case. */
bool typio_wl_text_ui_backend_update_content(TypioWlTextUiBackend *backend,
                                             const TypioPanelContent *content);

/* Status indicator (Phase 1 of unified panel backend).
 * These functions show or hide a transient status banner in the popup
 * surface, replacing the previous preedit-string hack for voice state.
 * Internally they build a TypioPanelContent and call update_content(). */
bool typio_wl_text_ui_backend_show_status(TypioWlTextUiBackend *backend,
                                          const char *text);
void typio_wl_text_ui_backend_hide_status(TypioWlTextUiBackend *backend);
void typio_wl_text_ui_backend_invalidate_config(TypioWlTextUiBackend *backend);
void typio_wl_text_ui_backend_handle_output_change(TypioWlTextUiBackend *backend,
                                                   struct wl_output *output);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_TEXT_UI_BACKEND_H */
