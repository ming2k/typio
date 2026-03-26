#ifndef TYPIO_WL_POPUP_STATE_H
#define TYPIO_WL_POPUP_STATE_H

#include <stdbool.h>

typedef struct TypioPopupInvalidationState {
    bool config_cache_valid;
    bool theme_cache_valid;
    bool render_cache_valid;
} TypioPopupInvalidationState;

typedef enum TypioPopupOutputChangeAction {
    TYPIO_POPUP_OUTPUT_CHANGE_IGNORE = 0,
    TYPIO_POPUP_OUTPUT_CHANGE_UNTRACK = 1,
    TYPIO_POPUP_OUTPUT_CHANGE_REFRESH = 2,
} TypioPopupOutputChangeAction;

void typio_popup_state_invalidate_config(TypioPopupInvalidationState *state);

TypioPopupOutputChangeAction typio_popup_state_handle_output_change(bool has_popup,
                                                                    bool tracks_output,
                                                                    bool output_known);

#endif /* TYPIO_WL_POPUP_STATE_H */
