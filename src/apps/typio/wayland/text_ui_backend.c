#include "text_ui_backend.h"

#include "wl_frontend_internal.h"

#include <stdlib.h>

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
    if (frontend->compositor && frontend->shm && frontend->input_method) {
        backend->candidate_popup = typio_wl_candidate_popup_create(frontend);
    }

    return backend;
}

void typio_wl_text_ui_backend_destroy(TypioWlTextUiBackend *backend) {
    if (!backend) {
        return;
    }

    if (backend->candidate_popup) {
        typio_wl_candidate_popup_destroy(backend->candidate_popup);
    }

    free(backend);
}

bool typio_wl_text_ui_backend_update(TypioWlTextUiBackend *backend,
                                     TypioInputContext *ctx) {
    if (!backend || !backend->frontend) {
        return false;
    }

    return typio_wl_candidate_popup_update(backend, ctx);
}

void typio_wl_text_ui_backend_hide(TypioWlTextUiBackend *backend) {
    if (!backend || !backend->frontend) {
        return;
    }

    typio_wl_candidate_popup_hide(backend);
}

bool typio_wl_text_ui_backend_is_available(TypioWlTextUiBackend *backend) {
    return typio_wl_candidate_popup_is_available(backend);
}

void typio_wl_text_ui_backend_invalidate_config(TypioWlTextUiBackend *backend) {
    if (!backend) {
        return;
    }

    typio_wl_candidate_popup_invalidate_config(backend);
}

void typio_wl_text_ui_backend_handle_output_change(TypioWlTextUiBackend *backend,
                                                   struct wl_output *output) {
    if (!backend) {
        return;
    }

    typio_wl_candidate_popup_handle_output_change(backend, output);
}
