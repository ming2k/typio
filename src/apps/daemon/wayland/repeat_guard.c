/**
 * @file repeat_guard.c
 * @brief Helpers for deciding when keyboard repeat may run or should be cancelled
 */

#include "repeat_guard.h"
#include "typio/types.h"

bool typio_wl_repeat_should_cancel_on_modifier_transition(
    uint32_t previous_modifiers,
    uint32_t current_modifiers) {
    uint32_t previous_blocking;
    uint32_t current_blocking;

    previous_blocking = previous_modifiers &
        (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER);
    current_blocking = current_modifiers &
        (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER);

    return previous_blocking != current_blocking;
}

bool typio_wl_repeat_should_run_for_state(TypioKeyTrackState state) {
    switch (state) {
    case TYPIO_KEY_TRACK_SUPPRESSED_STARTUP:
    case TYPIO_KEY_TRACK_RELEASED_PENDING:
        return false;
    default:
        return true;
    }
}
