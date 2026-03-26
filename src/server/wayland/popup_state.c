#include "popup_state.h"

void typio_popup_state_invalidate_config(TypioPopupInvalidationState *state) {
    if (!state) {
        return;
    }

    state->config_cache_valid = false;
    state->theme_cache_valid = false;
    state->render_cache_valid = false;
}

TypioPopupOutputChangeAction typio_popup_state_handle_output_change(bool has_popup,
                                                                    bool tracks_output,
                                                                    bool output_known) {
    if (!has_popup || !tracks_output) {
        return TYPIO_POPUP_OUTPUT_CHANGE_IGNORE;
    }

    if (!output_known) {
        return TYPIO_POPUP_OUTPUT_CHANGE_UNTRACK;
    }

    return TYPIO_POPUP_OUTPUT_CHANGE_REFRESH;
}
