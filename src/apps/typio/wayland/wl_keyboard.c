/**
 * @file wl_keyboard.c
 * @brief Keyboard grab lifecycle and event dispatch
 *
 * This file is intentionally thin: XKB logic lives in keyboard_xkb.c,
 * key routing lives in key_route.c, repeat logic lives in keyboard_repeat.c,
 * and guard / tracking utilities live in key_tracking.c.  Only grab
 * lifecycle and the top-level Wayland dispatcher remain here.
 *
 * @see docs/explanation/timing-model.md
 * @see ADR-003: Keyboard grab lifecycle
 */

#include "wl_frontend_internal.h"
#include "keyboard_xkb.h"
#include "key_arbiter.h"
#include "key_debug.h"
#include "key_route.h"
#include "key_tracking_access.h"
#include "keyboard_repeat.h"
#include "modifier_policy.h"
#include "repeat_guard.h"
#include "monotonic_time.h"
#include "startup_guard.h"
#include "vk_bridge.h"
#include "wl_trace.h"
#include "xkb_modifiers.h"
#include "typio/typio.h"
#include "utils/log.h"

#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <inttypes.h>

#define GUARD_FAILSAFE_WINDOW_MS  1500
#define GUARD_FAILSAFE_THRESHOLD  8

/* ── Tracing ─────────────────────────────────────────────────────── */

static void trace_key(TypioWlKeyboard *kb, const char *stage,
                      uint32_t key, uint32_t keysym,
                      uint32_t modifiers, uint32_t unicode,
                      TypioKeyTrackState state, const char *detail) {
    if (!kb) return;
    uint32_t xkb_mods = kb->xkb_state ? typio_wl_xkb_effective_modifiers(kb) : 0;
    char keysym_desc[64], unicode_desc[64];
    typio_wl_key_debug_format_keysym(keysym, keysym_desc, sizeof(keysym_desc));
    typio_wl_key_debug_format(unicode, unicode_desc, sizeof(unicode_desc));
    typio_wl_trace(kb->frontend, "key",
        "stage=%s keycode=%u keysym=0x%x %s route=%s mods=0x%x phys=0x%x xkb=0x%x keygen=%u activegen=%u %s detail=%s",
        stage, key, keysym, keysym_desc,
        typio_wl_key_tracking_state_name(state),
        modifiers, kb->physical_modifiers, xkb_mods,
        key_get_generation(kb->frontend, key),
        kb->frontend->active_key_generation,
        unicode_desc, detail ? detail : "-");
}

/* ── Guard / failsafe ────────────────────────────────────────────── */

static const char *guard_reason(TypioWlFrontend *fe) {
    if (!fe) return "no_frontend";
    if (!fe->session) return "no_session";
    if (!typio_wl_lifecycle_phase_allows_key_events(fe->lifecycle_phase))
        return "lifecycle_not_active";
    if (!fe->session->ctx) return "no_context";
    if (!typio_input_context_is_focused(fe->session->ctx)) return "not_focused";
    return nullptr;
}

static void guard_reset(TypioWlFrontend *fe) {
    if (!fe) return;
    fe->guard_reject_press_streak = 0;
    fe->guard_reject_press_window_start_ms = 0;
}

static void guard_trigger_failsafe(TypioWlKeyboard *kb, const char *reason,
                                   uint32_t key, uint32_t keysym,
                                   uint32_t modifiers) {
    if (!kb || !kb->frontend) return;
    TypioWlFrontend *fe = kb->frontend;
    typio_log(TYPIO_LOG_ERROR,
        "Guard stuck: reason=%s phase=%s key=%u keysym=0x%x mods=0x%x streak=%" PRIu64
        "; releasing grab and stopping",
        reason, typio_wl_lifecycle_phase_name(fe->lifecycle_phase),
        key, keysym, modifiers, fe->guard_reject_press_streak);
    typio_log_dump_recent_to_configured_path();
    typio_wl_keyboard_release_grab(kb);
    typio_wl_frontend_stop(fe);
}

static void guard_note_reject(TypioWlKeyboard *kb, uint32_t key,
                              uint32_t keysym, uint32_t modifiers,
                              uint32_t state) {
    if (!kb || !kb->frontend) return;
    TypioWlFrontend *fe = kb->frontend;
    const char *reason = guard_reason(fe);

    typio_log(state == WL_KEYBOARD_KEY_STATE_PRESSED ? TYPIO_LOG_WARNING : TYPIO_LOG_DEBUG,
        "Key rejected: reason=%s phase=%s key=%u keysym=0x%x mods=0x%x",
        reason, typio_wl_lifecycle_phase_name(fe->lifecycle_phase),
        key, keysym, modifiers);

    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;

    uint64_t now = typio_wl_monotonic_ms();
    if (fe->guard_reject_press_window_start_ms == 0 ||
        now - fe->guard_reject_press_window_start_ms > GUARD_FAILSAFE_WINDOW_MS) {
        fe->guard_reject_press_window_start_ms = now;
        fe->guard_reject_press_streak = 1;
    } else {
        fe->guard_reject_press_streak++;
    }

    if (fe->guard_reject_press_streak >= GUARD_FAILSAFE_THRESHOLD &&
        fe->lifecycle_phase != TYPIO_WL_PHASE_ACTIVATING) {
        guard_trigger_failsafe(kb, reason, key, keysym, modifiers);
    }
}

/* ── Tracking helpers ────────────────────────────────────────────── */

static void tracking_reset(TypioWlFrontend *fe) {
    if (!fe) return;
    typio_wl_key_tracking_reset(fe->key_states, TYPIO_WL_MAX_TRACKED_KEYS);
    typio_wl_key_tracking_reset_generations(fe->key_generations,
                                            TYPIO_WL_MAX_TRACKED_KEYS);
    guard_reset(fe);
}

/* ── Wayland listeners ───────────────────────────────────────────── */

static void on_keymap(void *data,
                      [[maybe_unused]] struct zwp_input_method_keyboard_grab_v2 *kb,
                      uint32_t format, int32_t fd, uint32_t size) {
    TypioWlKeyboard *keyboard = data;
    typio_wl_trace(keyboard ? keyboard->frontend : nullptr,
                   "keymap", "stage=received format=%u size=%u", format, size);
    typio_wl_keyboard_handle_keymap(keyboard, format, fd, size);
}

static void on_key(void *data,
                   [[maybe_unused]] struct zwp_input_method_keyboard_grab_v2 *kb,
                   [[maybe_unused]] uint32_t serial, uint32_t time,
                   uint32_t key, uint32_t state);

static void on_modifiers(void *data,
                         [[maybe_unused]] struct zwp_input_method_keyboard_grab_v2 *kb,
                         [[maybe_unused]] uint32_t serial,
                         uint32_t mods_depressed, uint32_t mods_latched,
                         uint32_t mods_locked, uint32_t group) {
    TypioWlKeyboard *keyboard = data;
    if (!keyboard) return;

    uint32_t prev = keyboard->xkb_state ? typio_wl_xkb_effective_modifiers(keyboard) : 0;
    typio_wl_keyboard_handle_modifiers(keyboard, mods_depressed, mods_latched,
                                        mods_locked, group);
    uint32_t cur = keyboard->xkb_state ? typio_wl_xkb_effective_modifiers(keyboard) : 0;

    if ((cur & (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) != 0)
        keyboard->saw_blocking_modifier = true;

    if (keyboard->repeating &&
        typio_wl_repeat_should_cancel_on_modifier_transition(prev, cur)) {
        typio_wl_keyboard_repeat_stop(keyboard);
    }

    typio_wl_vk_forward_modifiers(keyboard, mods_depressed, mods_latched,
                                  mods_locked, group);
}

static void on_repeat_info(void *data,
                           [[maybe_unused]] struct zwp_input_method_keyboard_grab_v2 *kb,
                           int32_t rate, int32_t delay) {
    TypioWlKeyboard *keyboard = data;
    if (!keyboard) return;
    keyboard->repeat_rate = rate;
    keyboard->repeat_delay = delay;
    typio_wl_trace(keyboard->frontend, "repeat",
                   "stage=info rate=%d delay=%d", rate, delay);
}

static const struct zwp_input_method_keyboard_grab_v2_listener grab_listener = {
    .keymap = on_keymap,
    .key = on_key,
    .modifiers = on_modifiers,
    .repeat_info = on_repeat_info,
};

/* ── Lifecycle ───────────────────────────────────────────────────── */

TypioWlKeyboard *typio_wl_keyboard_create(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->input_method) return nullptr;

    TypioWlKeyboard *kb = calloc(1, sizeof(TypioWlKeyboard));
    if (!kb) return nullptr;

    frontend->active_key_generation++;
    if (frontend->active_key_generation == 0) frontend->active_key_generation = 1;
    frontend->active_generation_owned_keys = false;
    frontend->active_generation_vk_dirty = false;
    atomic_store(&frontend->watchdog_armed, true);
    tracking_reset(frontend);

    kb->frontend = frontend;
    kb->suppress_stale_keys = true;
    kb->created_at_epoch = frontend->dispatch_epoch;
    typio_wl_key_arbiter_init(&kb->arbiter);

    kb->repeat_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (kb->repeat_timer_fd < 0)
        typio_log(TYPIO_LOG_WARNING, "Failed to create repeat timerfd");

    kb->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!kb->xkb_context) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create XKB context");
        goto fail;
    }

    kb->grab = zwp_input_method_v2_grab_keyboard(frontend->input_method);
    if (!kb->grab) {
        typio_log(TYPIO_LOG_ERROR, "Failed to grab keyboard");
        goto fail;
    }

    zwp_input_method_keyboard_grab_v2_add_listener(kb->grab, &grab_listener, kb);
    typio_wl_trace(frontend, "grab", "action=create status=ok");
    typio_log(TYPIO_LOG_DEBUG, "Keyboard grab created");
    typio_wl_frontend_emit_runtime_state_changed(frontend);
    return kb;

fail:
    if (kb->repeat_timer_fd >= 0) close(kb->repeat_timer_fd);
    if (kb->xkb_context) xkb_context_unref(kb->xkb_context);
    free(kb);
    return nullptr;
}

void typio_wl_keyboard_destroy(TypioWlKeyboard *keyboard) {
    if (!keyboard) return;
    atomic_store(&keyboard->frontend->watchdog_armed, false);
    typio_wl_vk_release_forwarded_keys(keyboard->frontend,
                                        typio_wl_key_tracking_state_name);
    typio_wl_keyboard_release_grab(keyboard);
    typio_wl_keyboard_repeat_stop(keyboard);
    tracking_reset(keyboard->frontend);

    if (keyboard->repeat_timer_fd >= 0) close(keyboard->repeat_timer_fd);
    if (keyboard->xkb_state) xkb_state_unref(keyboard->xkb_state);
    if (keyboard->xkb_keymap) xkb_keymap_unref(keyboard->xkb_keymap);
    if (keyboard->xkb_context) xkb_context_unref(keyboard->xkb_context);

    typio_wl_trace(keyboard->frontend, "grab", "action=destroy status=ok");
    typio_log(TYPIO_LOG_DEBUG, "Keyboard destroyed");
    typio_wl_frontend_emit_runtime_state_changed(keyboard->frontend);
    free(keyboard);
}

void typio_wl_keyboard_release_grab(TypioWlKeyboard *keyboard) {
    if (!keyboard || !keyboard->grab) return;
    zwp_input_method_keyboard_grab_v2_release(keyboard->grab);
    keyboard->grab = nullptr;
    typio_wl_trace(keyboard->frontend, "grab", "action=release status=ok");
    typio_log(TYPIO_LOG_DEBUG, "Keyboard grab released");
    typio_wl_frontend_emit_runtime_state_changed(keyboard->frontend);
}

/* ── Public process helpers ──────────────────────────────────────── */

void typio_wl_keyboard_process_key_press(TypioWlKeyboard *keyboard,
                                         TypioWlSession *session,
                                         uint32_t key, uint32_t keysym,
                                         uint32_t modifiers, uint32_t unicode,
                                         uint32_t time) {
    typio_wl_key_route_process_press(keyboard, session, key, keysym,
                                      modifiers, unicode, time);
    if (keyboard->repeat_rate > 0 && keyboard->xkb_keymap &&
        typio_wl_repeat_should_run_for_state(key_get_state(keyboard->frontend, key)) &&
        xkb_keymap_key_repeats(keyboard->xkb_keymap, key + 8)) {
        typio_wl_keyboard_repeat_maybe_start(keyboard, key, time, modifiers);
    }
}

void typio_wl_keyboard_process_key_release(TypioWlKeyboard *keyboard,
                                           TypioWlSession *session,
                                           uint32_t key, uint32_t keysym,
                                           uint32_t modifiers, uint32_t unicode,
                                           uint32_t time) {
    if (keyboard->repeating && keyboard->repeat_key == key)
        typio_wl_keyboard_repeat_stop(keyboard);
    typio_wl_key_route_process_release(keyboard, session, key, keysym,
                                        modifiers, unicode, time);
}

/* ── Main dispatcher ─────────────────────────────────────────────── */

static void on_key(void *data,
                   [[maybe_unused]] struct zwp_input_method_keyboard_grab_v2 *kb,
                   [[maybe_unused]] uint32_t serial, uint32_t time,
                   uint32_t key, uint32_t state) {
    TypioWlKeyboard *keyboard = data;
    TypioWlFrontend *frontend = keyboard->frontend;
    if (!keyboard->xkb_state) return;

    uint32_t keysym = typio_wl_keyboard_keysym(keyboard, key);
    uint32_t unicode = typio_wl_keyboard_unicode(keyboard, key);
    uint32_t modifiers = keyboard->physical_modifiers;
    if (keyboard->xkb_state)
        modifiers = typio_wl_modifier_policy_effective_modifiers(
            keyboard->physical_modifiers,
            typio_wl_xkb_effective_modifiers(keyboard),
            frontend->active_generation_owned_keys,
            keysym, state);

    /* Emergency exit shortcut */
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED &&
        typio_wl_key_route_reserved_action(&frontend->shortcuts, keysym, modifiers) ==
        TYPIO_WL_RESERVED_ACTION_EMERGENCY_EXIT) {
        trace_key(keyboard, "emergency-exit", key, keysym, modifiers, unicode,
                  key_get_state(frontend, key), "early path");
        typio_log(TYPIO_LOG_WARNING,
            "Emergency exit: key=%u keysym=0x%x mods=0x%x phase=%s",
            key, keysym, modifiers,
            typio_wl_lifecycle_phase_name(frontend->lifecycle_phase));
        typio_log_dump_recent_to_configured_path();
        typio_wl_keyboard_release_grab(keyboard);
        typio_wl_frontend_stop(frontend);
        return;
    }

    /* Routing guard */
    if (guard_reason(frontend)) {
        guard_note_reject(keyboard, key, keysym, modifiers, state);
        trace_key(keyboard,
                  state == WL_KEYBOARD_KEY_STATE_PRESSED ? "guard-reject-press" : "guard-reject-release",
                  key, keysym, modifiers, unicode,
                  key_get_state(frontend, key),
                  typio_wl_lifecycle_phase_name(frontend->lifecycle_phase));
        if (state == WL_KEYBOARD_KEY_STATE_RELEASED &&
            keyboard->repeating && keyboard->repeat_key == key)
            typio_wl_keyboard_repeat_stop(keyboard);
        return;
    }

    guard_reset(frontend);
    trace_key(keyboard,
              state == WL_KEYBOARD_KEY_STATE_PRESSED ? "dispatch-press" : "dispatch-release",
              key, keysym, modifiers, unicode,
              key_get_state(frontend, key), "dispatch");

    /* Update physical modifier state before routing */
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        if (keysym == XKB_KEY_Shift_L || keysym == XKB_KEY_Shift_R)
            keyboard->physical_modifiers |= TYPIO_MOD_SHIFT;
        else if (keysym == XKB_KEY_Control_L || keysym == XKB_KEY_Control_R)
            keyboard->physical_modifiers |= TYPIO_MOD_CTRL;
        else if (keysym == XKB_KEY_Alt_L || keysym == XKB_KEY_Alt_R)
            keyboard->physical_modifiers |= TYPIO_MOD_ALT;
        else if (keysym == XKB_KEY_Super_L || keysym == XKB_KEY_Super_R)
            keyboard->physical_modifiers |= TYPIO_MOD_SUPER;
    } else {
        if (keysym == XKB_KEY_Shift_L || keysym == XKB_KEY_Shift_R)
            keyboard->physical_modifiers &= ~TYPIO_MOD_SHIFT;
        else if (keysym == XKB_KEY_Control_L || keysym == XKB_KEY_Control_R)
            keyboard->physical_modifiers &= ~TYPIO_MOD_CTRL;
        else if (keysym == XKB_KEY_Alt_L || keysym == XKB_KEY_Alt_R)
            keyboard->physical_modifiers &= ~TYPIO_MOD_ALT;
        else if (keysym == XKB_KEY_Super_L || keysym == XKB_KEY_Super_R)
            keyboard->physical_modifiers &= ~TYPIO_MOD_SUPER;
    }

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
        typio_wl_key_arbiter_press(&keyboard->arbiter, keyboard, frontend->session,
                                   key, keysym, modifiers, unicode, time);
    else
        typio_wl_key_arbiter_release(
            &keyboard->arbiter, keyboard, frontend->session,
            key, keysym, modifiers, unicode, time);
}
