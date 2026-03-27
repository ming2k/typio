#ifndef TYPIO_WL_CANDIDATE_POPUP_STATE_H
#define TYPIO_WL_CANDIDATE_POPUP_STATE_H

#include <stdbool.h>

typedef struct TypioCandidatePopupInvalidationState {
    bool config_cache_valid;
    bool theme_cache_valid;
    bool render_cache_valid;
} TypioCandidatePopupInvalidationState;

typedef enum TypioCandidatePopupOutputChangeAction {
    TYPIO_CANDIDATE_POPUP_OUTPUT_CHANGE_IGNORE = 0,
    TYPIO_CANDIDATE_POPUP_OUTPUT_CHANGE_UNTRACK = 1,
    TYPIO_CANDIDATE_POPUP_OUTPUT_CHANGE_REFRESH = 2,
} TypioCandidatePopupOutputChangeAction;

void typio_candidate_popup_state_invalidate_config(TypioCandidatePopupInvalidationState *state);

TypioCandidatePopupOutputChangeAction typio_candidate_popup_state_handle_output_change(bool has_popup,
                                                                    bool tracks_output,
                                                                    bool output_known);

#endif /* TYPIO_WL_CANDIDATE_POPUP_STATE_H */
