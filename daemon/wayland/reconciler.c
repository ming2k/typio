/**
 * @file reconciler.c
 * @brief Desired-vs-actual lifecycle reconciler implementation
 */

#include "reconciler.h"

#include "lifecycle.h"
#include "lifecycle_state.h"
#include "monotonic_time.h"
#include "wl_frontend_internal.h"
#include "wl_trace.h"
#include "typio/engine_manager.h"
#include "typio/instance.h"
#include "typio/log.h"

#include <inttypes.h>

/*
 * How long a steady-state divergence must persist before the reconciler
 * acts. Comfortably longer than any legitimate activation/reactivation
 * handshake (which runs through transient phases the projection ignores
 * anyway), short enough that a wedged state self-heals within a couple
 * seconds of the user noticing nothing happens.
 */
#define TYPIO_WL_RECONCILE_THRESHOLD_MS 2000ULL

static bool reconcile_has_active_kbd_engine(TypioWlFrontend *frontend) {
    TypioEngineManager *manager;

    if (!frontend || !frontend->instance)
        return false;

    manager = typio_instance_get_engine_manager(frontend->instance);
    return manager && typio_engine_manager_get_active(manager) != nullptr;
}

void typio_wl_reconcile_tick(TypioWlFrontend *frontend) {
    TypioWlLifecycleState observed;
    TypioWlLifecyclePhase declared;
    bool agree;
    uint64_t now_ms;
    TypioWlReconcileAction action;

    if (!frontend)
        return;

    declared = frontend->lifecycle_phase;
    observed = typio_wl_lifecycle_observe(frontend);
    agree = typio_wl_lifecycle_state_agrees(&observed, declared);

    /* Benign non-divergence: focused with no grab is the expected steady
     * state when no keyboard engine is active (e.g. voice-only, or engine
     * load failed) — the frontend intentionally does not grab. Without
     * this gate the reconciler would repair in a 2s loop forever, since
     * the projection cannot tell "grab pending" from "grab not wanted". */
    if (!agree && observed.focus == TYPIO_WL_FOCUS_FOCUSED &&
        observed.grab == TYPIO_WL_GRAB_NONE &&
        !reconcile_has_active_kbd_engine(frontend)) {
        agree = true;
    }

    now_ms = typio_wl_monotonic_ms();

    action = typio_wl_reconcile_decide(agree, now_ms,
                                       &frontend->reconcile_divergence_since_ms,
                                       TYPIO_WL_RECONCILE_THRESHOLD_MS);

    switch (action) {
    case TYPIO_WL_RECONCILE_OK:
    case TYPIO_WL_RECONCILE_WAIT:
        return;

    case TYPIO_WL_RECONCILE_ARM:
        typio_wl_trace(frontend,
                       "reconcile",
                       "action=arm declared=%s conn=%s focus=%s grab=%s comp=%s",
                       typio_wl_lifecycle_phase_name(declared),
                       typio_wl_conn_state_name(observed.conn),
                       typio_wl_focus_state_name(observed.focus),
                       typio_wl_grab_state_name(observed.grab),
                       typio_wl_comp_state_name(observed.comp));
        return;

    case TYPIO_WL_RECONCILE_REPAIR:
        typio_log(TYPIO_LOG_WARNING,
                  "Reconcile: declared phase=%s disagrees with observed "
                  "(conn=%s focus=%s grab=%s) past threshold; forcing recovery",
                  typio_wl_lifecycle_phase_name(declared),
                  typio_wl_conn_state_name(observed.conn),
                  typio_wl_focus_state_name(observed.focus),
                  typio_wl_grab_state_name(observed.grab));
        /* The recovery path scrubs to a clean slate and rebuilds the grab
         * when we are still focused — identical to the resume repair, so
         * reuse it rather than duplicating teardown/regrab logic. */
        typio_wl_input_method_handle_resume(frontend, "reconcile", 0);
        return;
    }
}
