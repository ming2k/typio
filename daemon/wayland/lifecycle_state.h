/**
 * @file lifecycle_state.h
 * @brief Orthogonal lifecycle state axes for the Wayland frontend
 *
 * The legacy @c TypioWlLifecyclePhase (lifecycle.h) is a single enum that
 * conflates four independent concerns: whether we are connected to the
 * compositor, whether the input method is focused, whether a keyboard
 * grab is established, and whether a composition is in flight. That works
 * for the happy path but cannot express disagreements like "we believe we
 * are ACTIVE but the grab is gone" — which is exactly the state a
 * suspend/resume or a compositor restart leaves us in.
 *
 * This module decomposes the phase into four orthogonal axes and provides
 * a pure projection back to the legacy phase. The orthogonal state is
 * never stored as a second source of truth; it is *observed* from the
 * live frontend fields (see @c typio_wl_lifecycle_observe in lifecycle.c)
 * and compared, via the projection, against the phase the frontend
 * believes it is in. The reconciler uses that comparison to detect and
 * repair divergence.
 *
 * Migration note: call sites still read the legacy phase. This model is
 * additive — it gives the reconciler a richer view without forcing the
 * ~80 existing phase call sites to change at once. Folding the legacy
 * enum into a pure projection of these axes is a later, separable step.
 */

#ifndef TYPIO_WL_LIFECYCLE_STATE_H
#define TYPIO_WL_LIFECYCLE_STATE_H

#include "lifecycle.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TYPIO_WL_CONN_DISCONNECTED = 0,
    TYPIO_WL_CONN_CONNECTED,
} TypioWlConnState;

typedef enum {
    TYPIO_WL_FOCUS_UNFOCUSED = 0,
    TYPIO_WL_FOCUS_FOCUSED,
} TypioWlFocusState;

typedef enum {
    TYPIO_WL_GRAB_NONE = 0,
    TYPIO_WL_GRAB_PENDING_KEYMAP,
    TYPIO_WL_GRAB_READY,
} TypioWlGrabState;

typedef enum {
    TYPIO_WL_COMP_IDLE = 0,
    TYPIO_WL_COMP_COMPOSING,
} TypioWlCompState;

typedef struct {
    TypioWlConnState conn;
    TypioWlFocusState focus;
    TypioWlGrabState grab;
    TypioWlCompState comp;
} TypioWlLifecycleState;

const char *typio_wl_conn_state_name(TypioWlConnState s);
const char *typio_wl_focus_state_name(TypioWlFocusState s);
const char *typio_wl_grab_state_name(TypioWlGrabState s);
const char *typio_wl_comp_state_name(TypioWlCompState s);

/**
 * Project the orthogonal axes onto the legacy steady-state phase:
 *
 *   not connected OR not focused          -> INACTIVE
 *   focused, grab ready                   -> ACTIVE
 *   focused, grab not yet ready           -> ACTIVATING
 *
 * The transient DEACTIVATING phase has no steady-state representation and
 * is never produced; the reconciler treats DEACTIVATING as in-flight and
 * does not compare against it.
 */
TypioWlLifecyclePhase
typio_wl_lifecycle_project_phase(const TypioWlLifecycleState *state);

/**
 * True when @c observed (projected) and @c declared describe the same
 * steady state, OR @c declared is a transient (DEACTIVATING/ACTIVATING)
 * that the projection deliberately cannot match. Used by the reconciler
 * to decide whether the frontend's belief has diverged from reality.
 */
bool typio_wl_lifecycle_state_agrees(const TypioWlLifecycleState *observed,
                                     TypioWlLifecyclePhase declared);

struct TypioWlFrontend;

/**
 * Observe the orthogonal lifecycle axes from the live frontend fields.
 * A read-only snapshot of reality (connection, focus, grab, composition),
 * not a stored second source of truth. Implemented in lifecycle.c because
 * it must read the frontend struct; declared here alongside the type it
 * returns. The reconciler compares its projection against the frontend's
 * declared @c lifecycle_phase.
 */
TypioWlLifecycleState
typio_wl_lifecycle_observe(const struct TypioWlFrontend *frontend);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_LIFECYCLE_STATE_H */
