/**
 * @file reconciler.h
 * @brief Desired-vs-actual lifecycle reconciler for the Wayland frontend
 *
 * The frontend's @c lifecycle_phase records what we *believe* our state is,
 * driven by compositor events. After a suspend/resume or a compositor
 * restart the compositor may stop agreeing with us — e.g. it dropped our
 * keyboard grab but never sent a deactivate, leaving us convinced we are
 * ACTIVE while reality has no grab. Event-driven logic alone cannot fix
 * this because the triggering event never arrives.
 *
 * The reconciler closes that gap. Once per event-loop iteration it
 * observes the orthogonal lifecycle axes (the real connection/focus/grab
 * state), projects them to a steady-state phase, and compares against the
 * declared phase. A divergence must *persist* past a threshold before any
 * repair fires, so normal mid-handshake transients are never disturbed.
 * When a divergence persists, the reconciler forces a recovery (scrub +
 * regrab) so the frontend converges back onto reality.
 */

#ifndef TYPIO_WL_RECONCILER_H
#define TYPIO_WL_RECONCILER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct TypioWlFrontend;

/** Outcome of a single pure reconcile decision. */
typedef enum {
    TYPIO_WL_RECONCILE_OK = 0,    /**< Observed state agrees with declared. */
    TYPIO_WL_RECONCILE_ARM,       /**< Divergence first seen; start the timer. */
    TYPIO_WL_RECONCILE_WAIT,      /**< Divergence persists but below threshold. */
    TYPIO_WL_RECONCILE_REPAIR,    /**< Divergence outlived threshold; repair now. */
} TypioWlReconcileAction;

/**
 * Pure decision: given whether the observed and declared states agree,
 * the current time, and the timestamp the divergence was first seen
 * (0 = none tracked), decide what to do and update @c divergence_since_ms
 * in place.
 *
 *   agree                       -> OK,     divergence_since := 0
 *   diverge, none tracked       -> ARM,    divergence_since := now
 *   diverge, within threshold   -> WAIT,   divergence_since unchanged
 *   diverge, threshold exceeded -> REPAIR, divergence_since := 0
 *
 * Separated from the effectful tick so it can be unit-tested without a
 * frontend or real clock.
 */
TypioWlReconcileAction
typio_wl_reconcile_decide(bool agree,
                          uint64_t now_ms,
                          uint64_t *divergence_since_ms,
                          uint64_t threshold_ms);

/**
 * Per-iteration reconcile step. Observes the frontend, runs the pure
 * decision, and on REPAIR forces a recovery. Cheap; safe to call every
 * event-loop turn.
 */
void typio_wl_reconcile_tick(struct TypioWlFrontend *frontend);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_RECONCILER_H */
