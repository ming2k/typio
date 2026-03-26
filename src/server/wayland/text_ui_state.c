#include "text_ui_state.h"

#include <stdlib.h>
#include <string.h>

TypioWlTextUiPlan typio_wl_text_ui_plan_update(const char *last_preedit_text,
                                               int last_preedit_cursor,
                                               const char *next_preedit_text,
                                               int next_preedit_cursor) {
    const char *last_text = last_preedit_text ? last_preedit_text : "";
    const char *next_text = next_preedit_text ? next_preedit_text : "";

    if (last_preedit_cursor != next_preedit_cursor ||
        strcmp(last_text, next_text) != 0) {
        return TYPIO_WL_TEXT_UI_SYNC_PREEDIT_AND_POPUP;
    }

    return TYPIO_WL_TEXT_UI_DEFER_POPUP_ONLY;
}

void typio_wl_text_ui_reset_tracking(bool *popup_update_pending,
                                     char **last_preedit_text,
                                     int *last_preedit_cursor) {
    if (popup_update_pending) {
        *popup_update_pending = false;
    }

    if (last_preedit_text) {
        free(*last_preedit_text);
        *last_preedit_text = NULL;
    }

    if (last_preedit_cursor) {
        *last_preedit_cursor = -1;
    }
}

bool typio_wl_text_ui_should_flush_popup_update(bool popup_update_pending,
                                                bool has_session,
                                                bool has_context,
                                                bool context_focused) {
    return popup_update_pending && has_session && has_context && context_focused;
}
