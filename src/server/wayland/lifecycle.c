/**
 * @file lifecycle.c
 * @brief Lifecycle and timing helpers for Wayland input-method sessions
 */

#include "boundary_bridge.h"
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
    bool carry_vk_modifiers = false;

    if (!frontend)
        return;

    typio_wl_trace(frontend,
                   "lifecycle",
                   "action=hard_reset_keyboard reason=%s",
                   reason ? reason : "no reason");

    own_current_generation = frontend->active_generation_owned_keys;

    if (typio_wl_boundary_bridge_should_reset_carried_modifiers(
            frontend->lifecycle_phase,
            frontend->carried_vk_modifiers)) {
        typio_wl_vk_reset_modifiers(frontend);
    }

    if (own_current_generation) {
        typio_wl_vk_release_forwarded_keys(frontend, NULL);
        if (frontend->keyboard &&
            typio_wl_boundary_bridge_should_carry_modifiers(
                frontend->lifecycle_phase,
                own_current_generation,
                frontend->keyboard->mods_depressed,
                frontend->keyboard->mods_latched,
                frontend->keyboard->mods_locked)) {
            typio_wl_vk_forward_modifier_state(
                frontend,
                frontend->keyboard->mods_depressed,
                frontend->keyboard->mods_latched,
                frontend->keyboard->mods_locked,
                frontend->keyboard->mods_group);
            frontend->carried_vk_modifiers = true;
            carry_vk_modifiers = true;
            typio_wl_trace(frontend,
                           "vk_modifiers",
                           "reason=deactivate_carry status=preserved");
        } else {
            typio_wl_vk_reset_modifiers(frontend);
        }
    }

    if (frontend->keyboard) {
        typio_wl_keyboard_cancel_repeat(frontend->keyboard);
        typio_wl_keyboard_destroy(frontend->keyboard);
        frontend->keyboard = NULL;
    }

    frontend->active_generation_owned_keys = false;
    frontend->active_generation_vk_dirty = carry_vk_modifiers;
}
