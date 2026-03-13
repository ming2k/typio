/**
 * @file lifecycle_model.c
 * @brief Pure lifecycle timing rules for Wayland input-method sessions
 */

#include "lifecycle.h"

const char *typio_wl_lifecycle_phase_name(TypioWlLifecyclePhase phase) {
    switch (phase) {
    case TYPIO_WL_PHASE_INACTIVE:
        return "inactive";
    case TYPIO_WL_PHASE_ACTIVATING:
        return "activating";
    case TYPIO_WL_PHASE_ACTIVE:
        return "active";
    case TYPIO_WL_PHASE_DEACTIVATING:
        return "deactivating";
    default:
        return "unknown";
    }
}

bool typio_wl_lifecycle_transition_is_valid(TypioWlLifecyclePhase from,
                                            TypioWlLifecyclePhase to) {
    if (from == to)
        return true;

    switch (from) {
    case TYPIO_WL_PHASE_INACTIVE:
        return to == TYPIO_WL_PHASE_ACTIVATING;
    case TYPIO_WL_PHASE_ACTIVATING:
        return to == TYPIO_WL_PHASE_ACTIVE || to == TYPIO_WL_PHASE_INACTIVE;
    case TYPIO_WL_PHASE_ACTIVE:
        return to == TYPIO_WL_PHASE_DEACTIVATING;
    case TYPIO_WL_PHASE_DEACTIVATING:
        return to == TYPIO_WL_PHASE_INACTIVE || to == TYPIO_WL_PHASE_ACTIVATING;
    default:
        return false;
    }
}

bool typio_wl_lifecycle_phase_allows_key_events(TypioWlLifecyclePhase phase) {
    return phase == TYPIO_WL_PHASE_ACTIVE;
}

bool typio_wl_lifecycle_phase_allows_modifier_events(
    TypioWlLifecyclePhase phase) {
    return phase == TYPIO_WL_PHASE_ACTIVATING ||
           phase == TYPIO_WL_PHASE_ACTIVE;
}
