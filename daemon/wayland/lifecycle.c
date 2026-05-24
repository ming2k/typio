/**
 * @file lifecycle.c
 * @brief Lifecycle and timing helpers for Wayland input-method sessions
 */

#include "boundary_bridge.h"
#include "key_tracking.h"
#include "lifecycle.h"
#include "lifecycle_state.h"
#include "vk_bridge.h"
#include "wl_frontend_internal.h"
#include "wl_trace.h"
#include "typio/input_context.h"
#include "typio/log.h"

#include <inttypes.h>

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
    bool had_keyboard;

    if (!frontend)
        return;

    typio_wl_trace(frontend,
                   "lifecycle",
                   "action=hard_reset_keyboard reason=%s",
                   reason ? reason : "no reason");

    own_current_generation = frontend->active_generation_owned_keys;
    had_keyboard = frontend->keyboard != nullptr;
    typio_log(TYPIO_LOG_DEBUG,
              "Hard reset keyboard boundary: reason=%s phase=%s own_generation=%s "
              "keyboard=%s",
              reason ? reason : "no reason",
              typio_wl_lifecycle_phase_name(frontend->lifecycle_phase),
              own_current_generation ? "yes" : "no",
              had_keyboard ? "yes" : "no");

    if (typio_wl_boundary_bridge_should_reset_carried_modifiers(
            frontend->lifecycle_phase,
            frontend->carried_vk_modifiers)) {
        typio_wl_vk_reset_modifiers(frontend);
    }

    if (own_current_generation) {
        typio_wl_vk_release_forwarded_keys(frontend, nullptr);
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
        frontend->keyboard = nullptr;
    }

    typio_wl_vk_cancel_keymap_wait(frontend,
                                   "keyboard grab cleared before keymap");

    frontend->active_generation_owned_keys = false;
    frontend->active_generation_vk_dirty = carry_vk_modifiers;
}

void typio_wl_lifecycle_on_resume(TypioWlFrontend *frontend,
                                  const char *reason,
                                  uint64_t sleep_ms) {
    if (!frontend)
        return;

    typio_wl_trace(frontend,
                   "lifecycle",
                   "action=on_resume reason=%s sleep_ms=%" PRIu64 " phase=%s",
                   reason ? reason : "no reason",
                   sleep_ms,
                   typio_wl_lifecycle_phase_name(frontend->lifecycle_phase));
    typio_log(TYPIO_LOG_INFO,
              "Lifecycle scrub: reason=%s sleep_ms=%" PRIu64 " phase=%s",
              reason ? reason : "no reason",
              sleep_ms,
              typio_wl_lifecycle_phase_name(frontend->lifecycle_phase));

    /* Tear the grab down *before* touching the generation/tracking arrays:
     * hard_reset_keyboard walks key_states to release any keys we forwarded
     * to the client, so it must see the pre-suspend state. After it returns
     * the grab is gone and forwarded keys have synthetic releases queued. */
    typio_wl_lifecycle_hard_reset_keyboard(frontend, reason ? reason : "resume");

    /* A modifier held across suspend never produced a key-up, and any
     * carried virtual-keyboard modifier state is now stale. Drop it
     * unconditionally — unlike a normal deactivate, a resume must not
     * preserve modifiers across the boundary. */
    typio_wl_vk_reset_modifiers(frontend);
    frontend->carried_vk_modifiers = false;
    frontend->active_generation_vk_dirty = false;

    /* Bump the key generation so any stale key event the compositor
     * re-delivers from before the suspend is fenced out at the routing
     * boundary, then clear the per-key tracking and generation arrays. */
    frontend->active_key_generation++;
    if (frontend->active_key_generation == 0)
        frontend->active_key_generation = 1;
    typio_wl_key_tracking_reset(frontend->key_states,
                                TYPIO_WL_MAX_TRACKED_KEYS);
    typio_wl_key_tracking_reset_generations(frontend->key_generations,
                                            TYPIO_WL_MAX_TRACKED_KEYS);

    /* Drop the compositor-visible preedit defensively. The engine's
     * session state is untouched, so a subsequent focus_in re-emits it. */
    typio_wl_set_preedit(frontend, "", -1, -1);
    typio_wl_commit(frontend);

    /* Force the phase back to INACTIVE so the next activate runs the full
     * activation path. A direct ACTIVE->INACTIVE is not a normal
     * transition; route through DEACTIVATING to keep the state-machine
     * validator quiet and the trace readable. */
    if (frontend->lifecycle_phase == TYPIO_WL_PHASE_ACTIVE) {
        typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_DEACTIVATING,
                                     "resume scrub");
    }
    if (frontend->lifecycle_phase != TYPIO_WL_PHASE_INACTIVE) {
        typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_INACTIVE,
                                     "resume scrub");
    }
    frontend->pending_reactivation = false;
}

TypioWlLifecycleState
typio_wl_lifecycle_observe(const TypioWlFrontend *frontend) {
    TypioWlLifecycleState state = {
        .conn = TYPIO_WL_CONN_DISCONNECTED,
        .focus = TYPIO_WL_FOCUS_UNFOCUSED,
        .grab = TYPIO_WL_GRAB_NONE,
        .comp = TYPIO_WL_COMP_IDLE,
    };

    if (!frontend)
        return state;

    if (frontend->display)
        state.conn = TYPIO_WL_CONN_CONNECTED;

    if (frontend->session && frontend->session->ctx &&
        typio_input_context_is_focused(frontend->session->ctx))
        state.focus = TYPIO_WL_FOCUS_FOCUSED;

    /* Grab readiness means the keyboard grab object exists, i.e. the
     * engine can receive keys. It deliberately does NOT fold in the
     * virtual-keyboard keymap handshake: a grab whose vk side is degraded
     * still routes keys to the engine and must not be torn down by the
     * reconciler. vk health is tracked separately (typio_wl_vk_health_check). */
    if (frontend->keyboard)
        state.grab = TYPIO_WL_GRAB_READY;

    if (frontend->session && frontend->session->last_preedit_text &&
        frontend->session->last_preedit_text[0])
        state.comp = TYPIO_WL_COMP_COMPOSING;

    return state;
}
