#include "text_ui_backend.h"

#include "wl_frontend_internal.h"
#include "typio/input_context.h"

#include <stdlib.h>
#include <string.h>

TypioWlTextUiBackend *typio_wl_text_ui_backend_create(TypioWlFrontend *frontend) {
    TypioWlTextUiBackend *backend;

    if (!frontend) {
        return nullptr;
    }

    backend = calloc(1, sizeof(*backend));
    if (!backend) {
        return nullptr;
    }

    backend->frontend = frontend;
    if (frontend->compositor && frontend->input_method) {
        backend->candidate_panel = typio_wl_candidate_panel_create(frontend);
    }

    return backend;
}

void typio_wl_text_ui_backend_destroy(TypioWlTextUiBackend *backend) {
    if (!backend) {
        return;
    }

    if (backend->candidate_panel) {
        typio_wl_candidate_panel_destroy(backend->candidate_panel);
    }

    free(backend);
}

bool typio_wl_text_ui_backend_update_content(TypioWlTextUiBackend *backend,
                                             const TypioPanelContent *content) {
    if (!backend || !backend->frontend) {
        return false;
    }

    return typio_wl_candidate_panel_update_content(backend, content);
}

bool typio_wl_text_ui_backend_update(TypioWlTextUiBackend *backend,
                                     TypioInputContext *ctx) {
    if (!backend || !backend->frontend) {
        return false;
    }

    /* Convenience wrapper: build a content descriptor from InputContext alone.
     * The status field is left inactive so that any persistent voice status
     * stored in the popup is preserved. */
    TypioPanelContent content;
    typio_panel_content_init(&content);
    if (ctx) {
        content.input.candidates = typio_input_context_get_candidates(ctx);
        content.input.preedit    = typio_input_context_get_preedit(ctx);
    }
    return typio_wl_candidate_panel_update_content(backend, &content);
}

void typio_wl_text_ui_backend_hide(TypioWlTextUiBackend *backend) {
    if (!backend || !backend->frontend) {
        return;
    }

    typio_wl_candidate_panel_hide(backend);
}

bool typio_wl_text_ui_backend_is_available(TypioWlTextUiBackend *backend) {
    return typio_wl_candidate_panel_is_available(backend);
}

void typio_wl_text_ui_backend_invalidate_config(TypioWlTextUiBackend *backend) {
    if (!backend) {
        return;
    }

    typio_wl_candidate_panel_invalidate_config(backend);
}

void typio_wl_text_ui_backend_handle_output_change(TypioWlTextUiBackend *backend,
                                                   struct wl_output *output) {
    if (!backend) {
        return;
    }

    typio_wl_candidate_panel_handle_output_change(backend, output);
}

bool typio_wl_text_ui_backend_show_status(TypioWlTextUiBackend *backend,
                                          const char *text) {
    if (!backend || !backend->frontend) {
        return false;
    }

    TypioPanelContent content;
    typio_panel_content_init(&content);
    if (text && text[0]) {
        content.status.active  = true;
        content.status.message = text;
    }
    return typio_wl_candidate_panel_update_content(backend, &content);
}

void typio_wl_text_ui_backend_hide_status(TypioWlTextUiBackend *backend) {
    if (!backend) {
        return;
    }

    TypioPanelContent content;
    typio_panel_content_init(&content);
    content.status.active  = false;
    content.status.message = "";  /* empty string signals explicit clear */
    typio_wl_candidate_panel_update_content(backend, &content);
}
