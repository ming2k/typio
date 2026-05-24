/**
 * @file candidate_guard.c
 * @brief Helpers for reserving candidate navigation keys from app passthrough
 */

#include "candidate_guard.h"

#include <xkbcommon/xkbcommon-keysyms.h>

bool typio_wl_candidate_guard_is_navigation_keysym(uint32_t keysym) {
    switch (keysym) {
    case XKB_KEY_Up:
    case XKB_KEY_Down:
    case XKB_KEY_Left:
    case XKB_KEY_Right:
        return true;
    default:
        return false;
    }
}

bool typio_wl_candidate_guard_should_consume(TypioInputContext *ctx,
                                             uint32_t keysym) {
    const TypioCandidateList *candidates;

    if (!ctx || !typio_wl_candidate_guard_is_navigation_keysym(keysym))
        return false;

    candidates = typio_input_context_get_candidates(ctx);
    return candidates && candidates->count > 0;
}
