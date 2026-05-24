/**
 * @file reconcile_model.c
 * @brief Pure reconcile decision rule
 *
 * Dependency-free (reconciler.h only) so the divergence/threshold logic
 * can be unit-tested without a frontend or real clock. The effectful tick
 * lives in reconciler.c.
 */

#include "reconciler.h"

TypioWlReconcileAction
typio_wl_reconcile_decide(bool agree,
                          uint64_t now_ms,
                          uint64_t *divergence_since_ms,
                          uint64_t threshold_ms) {
    if (!divergence_since_ms)
        return TYPIO_WL_RECONCILE_OK;

    if (agree) {
        *divergence_since_ms = 0;
        return TYPIO_WL_RECONCILE_OK;
    }

    if (*divergence_since_ms == 0) {
        *divergence_since_ms = now_ms;
        return TYPIO_WL_RECONCILE_ARM;
    }

    if (now_ms >= *divergence_since_ms &&
        now_ms - *divergence_since_ms >= threshold_ms) {
        *divergence_since_ms = 0;
        return TYPIO_WL_RECONCILE_REPAIR;
    }

    return TYPIO_WL_RECONCILE_WAIT;
}
