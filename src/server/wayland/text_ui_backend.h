#ifndef TYPIO_WL_TEXT_UI_BACKEND_H
#define TYPIO_WL_TEXT_UI_BACKEND_H

#include <stdbool.h>

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
void typio_wl_text_ui_backend_invalidate_config(TypioWlTextUiBackend *backend);
void typio_wl_text_ui_backend_handle_output_change(TypioWlTextUiBackend *backend,
                                                   struct wl_output *output);

#endif /* TYPIO_WL_TEXT_UI_BACKEND_H */
