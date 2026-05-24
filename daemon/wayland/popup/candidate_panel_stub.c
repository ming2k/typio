/**
 * @file candidate_panel_stub.c
 * @brief No-op stub for candidate panel when flux is unavailable.
 */

#include "wl_frontend_internal.h"
#include "text_ui_backend.h"

#include <stdlib.h>

TypioWlCandidatePanel *typio_wl_candidate_panel_create(TypioWlFrontend *frontend) {
    (void)frontend;
    return NULL;
}

void typio_wl_candidate_panel_destroy(TypioWlCandidatePanel *popup) {
    (void)popup;
}

bool typio_wl_candidate_panel_update_content(TypioWlTextUiBackend *backend,
                                             const TypioPanelContent *content) {
    (void)backend;
    (void)content;
    return false;
}

bool typio_wl_candidate_panel_update(TypioWlTextUiBackend *backend, TypioInputContext *ctx) {
    (void)backend;
    (void)ctx;
    return false;
}

void typio_wl_candidate_panel_hide(TypioWlTextUiBackend *backend) {
    (void)backend;
}

bool typio_wl_candidate_panel_is_available(TypioWlTextUiBackend *backend) {
    (void)backend;
    return false;
}

bool typio_wl_candidate_panel_present_retry_pending(TypioWlTextUiBackend *backend) {
    (void)backend;
    return false;
}

void typio_wl_candidate_panel_invalidate_config(TypioWlTextUiBackend *backend) {
    (void)backend;
}

void typio_wl_candidate_panel_handle_output_change(TypioWlTextUiBackend *backend,
                                                   struct wl_output *output) {
    (void)backend;
    (void)output;
}

bool typio_wl_candidate_panel_show_status(TypioWlTextUiBackend *backend,
                                          const char *text) {
    (void)backend;
    (void)text;
    return false;
}
