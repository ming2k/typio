/**
 * @file key_tracking.c
 * @brief Helpers for managing per-key forwarding state across lifecycle boundaries
 */

#include "key_tracking.h"

#include <string.h>

void typio_wl_key_tracking_reset(TypioKeyTrackState *states, size_t count) {
    if (!states || count == 0)
        return;

    memset(states, 0, count * sizeof(*states));
}

void typio_wl_key_tracking_reset_generations(uint32_t *generations,
                                             size_t count) {
    if (!generations || count == 0)
        return;

    memset(generations, 0, count * sizeof(*generations));
}

size_t typio_wl_key_tracking_mark_released_pending(TypioKeyTrackState *states,
                                                   size_t count) {
    size_t changed = 0;

    if (!states || count == 0)
        return 0;

    for (size_t i = 0; i < count; ++i) {
        if (states[i] != TYPIO_KEY_FORWARDED &&
            states[i] != TYPIO_KEY_APP_SHORTCUT)
            continue;

        states[i] = TYPIO_KEY_RELEASED_PENDING;
        changed++;
    }

    return changed;
}
