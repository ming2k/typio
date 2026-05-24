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

bool typio_wl_lifecycle_should_defer_activate(bool session_focused) {
    return session_focused;
}

bool typio_wl_lifecycle_should_cleanup_on_done(bool was_active,
                                               bool now_active) {
    return was_active && !now_active;
}

bool typio_wl_lifecycle_should_commit_reactivation(bool pending_reactivation,
                                                   bool was_active,
                                                   bool now_active) {
    return pending_reactivation && was_active && now_active;
}
