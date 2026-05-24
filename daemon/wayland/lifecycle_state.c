/**
 * @file lifecycle_state.c
 * @brief Pure projection between orthogonal lifecycle axes and the legacy phase
 *
 * Dependency-free on purpose: this file knows nothing about the frontend
 * struct so the projection can be unit-tested in isolation. The bridge
 * from live frontend fields to these axes lives in lifecycle.c
 * (typio_wl_lifecycle_observe).
 */

#include "lifecycle_state.h"

const char *typio_wl_conn_state_name(TypioWlConnState s) {
    switch (s) {
    case TYPIO_WL_CONN_DISCONNECTED: return "disconnected";
    case TYPIO_WL_CONN_CONNECTED:    return "connected";
    default:                         return "unknown";
    }
}

const char *typio_wl_focus_state_name(TypioWlFocusState s) {
    switch (s) {
    case TYPIO_WL_FOCUS_UNFOCUSED: return "unfocused";
    case TYPIO_WL_FOCUS_FOCUSED:   return "focused";
    default:                       return "unknown";
    }
}

const char *typio_wl_grab_state_name(TypioWlGrabState s) {
    switch (s) {
    case TYPIO_WL_GRAB_NONE:           return "none";
    case TYPIO_WL_GRAB_PENDING_KEYMAP: return "pending_keymap";
    case TYPIO_WL_GRAB_READY:          return "ready";
    default:                           return "unknown";
    }
}

const char *typio_wl_comp_state_name(TypioWlCompState s) {
    switch (s) {
    case TYPIO_WL_COMP_IDLE:      return "idle";
    case TYPIO_WL_COMP_COMPOSING: return "composing";
    default:                      return "unknown";
    }
}

TypioWlLifecyclePhase
typio_wl_lifecycle_project_phase(const TypioWlLifecycleState *state) {
    if (!state)
        return TYPIO_WL_PHASE_INACTIVE;

    if (state->conn != TYPIO_WL_CONN_CONNECTED ||
        state->focus != TYPIO_WL_FOCUS_FOCUSED)
        return TYPIO_WL_PHASE_INACTIVE;

    if (state->grab == TYPIO_WL_GRAB_READY)
        return TYPIO_WL_PHASE_ACTIVE;

    return TYPIO_WL_PHASE_ACTIVATING;
}

bool typio_wl_lifecycle_state_agrees(const TypioWlLifecycleState *observed,
                                     TypioWlLifecyclePhase declared) {
    TypioWlLifecyclePhase projected;

    /* Transient phases are mid-handshake; the steady-state projection
     * cannot represent them, so never treat them as divergence. */
    if (declared == TYPIO_WL_PHASE_DEACTIVATING ||
        declared == TYPIO_WL_PHASE_ACTIVATING)
        return true;

    projected = typio_wl_lifecycle_project_phase(observed);
    return projected == declared;
}
