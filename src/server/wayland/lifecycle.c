/**
 * @file lifecycle.c
 * @brief Lifecycle and timing helpers for Wayland input-method sessions
 */

#include "lifecycle.h"
#include "wl_frontend_internal.h"
#include "wl_trace.h"
#include "utils/log.h"

void typio_wl_lifecycle_set_phase(TypioWlFrontend *frontend,
                                  TypioWlLifecyclePhase phase,
                                  const char *reason) {
    TypioWlLifecyclePhase previous;

    if (!frontend)
        return;

    previous = frontend->lifecycle_phase;
    if (!typio_wl_lifecycle_transition_is_valid(previous, phase)) {
        typio_wl_trace_level(TYPIO_LOG_WARNING,
                             frontend,
                             "lifecycle",
                             "from=%s to=%s reason=%s status=unusual",
                             typio_wl_lifecycle_phase_name(previous),
                             typio_wl_lifecycle_phase_name(phase),
                             reason ? reason : "no reason");
    } else {
        typio_wl_trace(frontend,
                       "lifecycle",
                       "from=%s to=%s reason=%s status=ok",
                       typio_wl_lifecycle_phase_name(previous),
                       typio_wl_lifecycle_phase_name(phase),
                       reason ? reason : "no reason");
    }

    frontend->lifecycle_phase = phase;
}

void typio_wl_lifecycle_hard_reset_keyboard(TypioWlFrontend *frontend,
                                            const char *reason) {
    bool own_current_generation;

    if (!frontend)
        return;

    typio_wl_trace(frontend,
                   "lifecycle",
                   "action=hard_reset_keyboard reason=%s",
                   reason ? reason : "no reason");

    own_current_generation = frontend->active_generation_owned_keys;

    if (own_current_generation) {
        typio_wl_vk_release_forwarded_keys(frontend, NULL);
        typio_wl_vk_reset_modifiers(frontend);
    }

    if (frontend->keyboard) {
        typio_wl_keyboard_cancel_repeat(frontend->keyboard);
        typio_wl_keyboard_destroy(frontend->keyboard);
        frontend->keyboard = NULL;
    }

    frontend->active_generation_owned_keys = false;
    frontend->active_generation_vk_dirty = false;
}
