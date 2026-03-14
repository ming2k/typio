/**
 * @file lifecycle.h
 * @brief Lifecycle and timing helpers for Wayland input-method sessions
 */

#ifndef TYPIO_WL_LIFECYCLE_H
#define TYPIO_WL_LIFECYCLE_H

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
bool typio_wl_lifecycle_should_commit_reactivation(bool pending_reactivation,
                                                   bool was_active,
                                                   bool now_active);

void typio_wl_lifecycle_set_phase(struct TypioWlFrontend *frontend,
                                  TypioWlLifecyclePhase phase,
                                  const char *reason);
void typio_wl_lifecycle_hard_reset_keyboard(struct TypioWlFrontend *frontend,
                                            const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_LIFECYCLE_H */
