/**
 * @file lifecycle.h
 * @brief Lifecycle and timing helpers for Wayland input-method sessions
 */

#ifndef TYPIO_WL_LIFECYCLE_H
#define TYPIO_WL_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TYPIO_WL_PHASE_INACTIVE = 0,
    TYPIO_WL_PHASE_ACTIVATING,
    TYPIO_WL_PHASE_ACTIVE,
    TYPIO_WL_PHASE_DEACTIVATING,
} TypioWlLifecyclePhase;

struct TypioWlFrontend;
struct TypioWlKeyboard;

const char *typio_wl_lifecycle_phase_name(TypioWlLifecyclePhase phase);
bool typio_wl_lifecycle_transition_is_valid(TypioWlLifecyclePhase from,
                                            TypioWlLifecyclePhase to);
bool typio_wl_lifecycle_phase_allows_key_events(TypioWlLifecyclePhase phase);
bool typio_wl_lifecycle_phase_allows_modifier_events(
    TypioWlLifecyclePhase phase);
bool typio_wl_lifecycle_should_defer_activate(bool session_focused);
bool typio_wl_lifecycle_should_cleanup_on_done(bool was_active,
                                               bool now_active);
bool typio_wl_lifecycle_should_commit_reactivation(bool pending_reactivation,
                                                   bool was_active,
                                                   bool now_active);

void typio_wl_lifecycle_set_phase(struct TypioWlFrontend *frontend,
                                  TypioWlLifecyclePhase phase,
                                  const char *reason);
void typio_wl_lifecycle_hard_reset_keyboard(struct TypioWlFrontend *frontend,
                                            const char *reason);

/**
 * Drop every piece of in-flight input-method state that could plausibly
 * be stale across a system suspend: the keyboard grab, per-key tracking
 * and generations, any active repeat, the carried virtual-keyboard
 * modifiers, and the compositor-visible preedit. The lifecycle phase is
 * forced back to INACTIVE so the next @c activate from the compositor
 * goes through the full activation sequence and rebuilds a fresh grab.
 *
 * Called from @c resume_signal — both the logind @c PrepareForSleep
 * subscriber and the boottime-gap detector funnel here. The handler is
 * idempotent: firing twice for the same wake-up is safe.
 *
 * @param reason   stable string identifying the source ("logind",
 *                 "boottime_gap"); appears in trace output.
 * @param sleep_ms wall-clock ms the system slept for, or 0 if the
 *                 detector couldn't compute one (logind doesn't tell us).
 */
void typio_wl_lifecycle_on_resume(struct TypioWlFrontend *frontend,
                                  const char *reason,
                                  uint64_t sleep_ms);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_LIFECYCLE_H */
