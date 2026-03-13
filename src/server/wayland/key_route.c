/**
 * @file key_route.c
 * @brief Key press/release routing for Wayland keyboard events
 */

#define _POSIX_C_SOURCE 200809L

#include "key_route.h"

#include "boundary_bridge.h"
#include "key_debug.h"
#include "shortcut_chord.h"
#include "startup_guard.h"
#include "vk_bridge.h"
#include "wl_frontend_internal.h"
#include "wl_trace.h"
#include "typio/typio.h"
#include "typio/engine_manager.h"
#include "utils/log.h"

#include <time.h>
#include <xkbcommon/xkbcommon-keysyms.h>

static uint64_t key_route_monotonic_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;

    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

static uint32_t key_route_xkb_modifiers(TypioWlKeyboard *keyboard) {
    struct xkb_state *state = keyboard->xkb_state;
    uint32_t mods = TYPIO_MOD_NONE;

    if (xkb_state_mod_index_is_active(state, keyboard->mod_shift, XKB_STATE_MODS_EFFECTIVE))
        mods |= TYPIO_MOD_SHIFT;
    if (xkb_state_mod_index_is_active(state, keyboard->mod_ctrl, XKB_STATE_MODS_EFFECTIVE))
        mods |= TYPIO_MOD_CTRL;
    if (xkb_state_mod_index_is_active(state, keyboard->mod_alt, XKB_STATE_MODS_EFFECTIVE))
        mods |= TYPIO_MOD_ALT;
    if (xkb_state_mod_index_is_active(state, keyboard->mod_super, XKB_STATE_MODS_EFFECTIVE))
        mods |= TYPIO_MOD_SUPER;
    if (xkb_state_mod_index_is_active(state, keyboard->mod_caps, XKB_STATE_MODS_EFFECTIVE))
        mods |= TYPIO_MOD_CAPSLOCK;
    if (xkb_state_mod_index_is_active(state, keyboard->mod_num, XKB_STATE_MODS_EFFECTIVE))
        mods |= TYPIO_MOD_NUMLOCK;

    return mods;
}

static const char *key_route_state_name(TypioKeyTrackState state) {
    switch (state) {
    case TYPIO_KEY_IDLE:
        return "idle";
    case TYPIO_KEY_FORWARDED:
        return "forwarded";
    case TYPIO_KEY_APP_SHORTCUT:
        return "app_shortcut";
    case TYPIO_KEY_RELEASED_PENDING:
        return "released_pending";
    case TYPIO_KEY_SUPPRESSED_STARTUP:
        return "suppressed_startup";
    case TYPIO_KEY_SUPPRESSED_ENTER:
        return "suppressed_enter";
    default:
        return "unknown";
    }
}

static void key_route_trace(TypioWlKeyboard *keyboard,
                            const char *stage,
                            uint32_t key,
                            uint32_t keysym,
                            uint32_t modifiers,
                            uint32_t unicode,
                            TypioKeyTrackState state,
                            const char *detail) {
    uint32_t xkb_mods = 0;
    char keysym_desc[64];
    char unicode_desc[64];

    if (!keyboard)
        return;

    if (keyboard->xkb_state)
        xkb_mods = key_route_xkb_modifiers(keyboard);
    typio_wl_key_debug_format_keysym(keysym, keysym_desc, sizeof(keysym_desc));
    typio_wl_key_debug_format(unicode, unicode_desc, sizeof(unicode_desc));

    typio_wl_trace(keyboard->frontend,
                   "key",
                   "stage=%s keycode=%u keysym=0x%x %s route=%s mods=0x%x phys=0x%x xkb=0x%x keygen=%u activegen=%u %s detail=%s",
                   stage ? stage : "unknown",
                   key,
                   keysym,
                   keysym_desc,
                   key_route_state_name(state),
                   modifiers,
                   keyboard->physical_modifiers,
                   xkb_mods,
                   key < TYPIO_WL_MAX_TRACKED_KEYS ? keyboard->frontend->key_generations[key] : 0,
                   keyboard->frontend->active_key_generation,
                   unicode_desc,
                   detail ? detail : "-");
}

static inline TypioKeyTrackState key_get_state(TypioWlFrontend *fe, uint32_t key) {
    return (key < TYPIO_WL_MAX_TRACKED_KEYS) ? fe->key_states[key] : TYPIO_KEY_IDLE;
}

static inline void key_set_state(TypioWlFrontend *fe, uint32_t key,
                                 TypioKeyTrackState st) {
    if (key < TYPIO_WL_MAX_TRACKED_KEYS)
        fe->key_states[key] = st;
}

static inline uint32_t key_get_generation(TypioWlFrontend *fe, uint32_t key) {
    return (key < TYPIO_WL_MAX_TRACKED_KEYS) ? fe->key_generations[key] : 0;
}

static inline void key_set_generation(TypioWlFrontend *fe, uint32_t key,
                                      uint32_t generation) {
    if (key < TYPIO_WL_MAX_TRACKED_KEYS)
        fe->key_generations[key] = generation;
}

static inline void key_claim_current_generation(TypioWlFrontend *fe, uint32_t key) {
    key_set_generation(fe, key, fe->active_key_generation);
    fe->active_generation_owned_keys = true;
}

static inline void key_clear_tracking(TypioWlFrontend *fe, uint32_t key) {
    key_set_state(fe, key, TYPIO_KEY_IDLE);
    key_set_generation(fe, key, 0);
}

static inline bool key_owned_by_active_generation(TypioWlFrontend *fe, uint32_t key) {
    return fe && fe->active_key_generation != 0 &&
           key_get_generation(fe, key) == fe->active_key_generation;
}

static bool key_route_is_app_shortcut(uint32_t keysym, uint32_t modifiers) {
    if ((modifiers & (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) == 0)
        return false;

    switch (keysym) {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
        return false;
    default:
        return true;
    }
}

static void key_route_maybe_switch_engine_on_modifier_chord(
    TypioWlKeyboard *keyboard,
    uint32_t keysym,
    uint32_t modifiers) {
    TypioWlFrontend *frontend;
    TypioEngineManager *manager;

    if (!keyboard || !keyboard->frontend || !keyboard->frontend->instance)
        return;

    frontend = keyboard->frontend;
    if (!typio_wl_shortcut_chord_should_switch_engine(
            keysym,
            modifiers,
            frontend->shortcut_chord_saw_non_modifier,
            frontend->shortcut_chord_switch_triggered)) {
        return;
    }

    manager = typio_instance_get_engine_manager(frontend->instance);
    if (!manager)
        return;

    if (typio_engine_manager_next(manager) == TYPIO_OK) {
        frontend->shortcut_chord_switch_triggered = true;
        key_route_trace(keyboard, "shortcut-switch", 0, keysym, modifiers, 0,
                        TYPIO_KEY_IDLE, "ctrl+shift engine switch");
        typio_log(TYPIO_LOG_INFO, "Switched engine via Ctrl+Shift chord");
    }
}

void typio_wl_key_route_process_press(TypioWlKeyboard *keyboard,
                                      TypioWlSession *session,
                                      uint32_t key,
                                      uint32_t keysym,
                                      uint32_t modifiers,
                                      uint32_t unicode,
                                      uint32_t time) {
    TypioWlFrontend *frontend = keyboard->frontend;
    TypioKeyTrackState kstate = key_get_state(frontend, key);
    uint64_t now_ms = key_route_monotonic_ms();
    TypioWlStartupSuppressReason suppress_reason;
    const TypioPreedit *preedit = typio_input_context_get_preedit(session->ctx);
    const TypioCandidateList *candidates =
        typio_input_context_get_candidates(session->ctx);
    bool has_composition = (preedit && preedit->segment_count > 0) ||
                           (candidates && candidates->count > 0);

    if (kstate == TYPIO_KEY_RELEASED_PENDING) {
        key_route_trace(keyboard, "press-ignore", key, keysym, modifiers, unicode,
                        kstate, "released pending");
        typio_log(TYPIO_LOG_DEBUG,
                  "Suppressing press for force-released key: keycode=%u", key);
        return;
    }

    if (kstate == TYPIO_KEY_SUPPRESSED_STARTUP ||
        kstate == TYPIO_KEY_SUPPRESSED_ENTER) {
        key_route_trace(keyboard, "press-ignore", key, keysym, modifiers, unicode,
                        kstate, "startup guarded repeat");
        typio_log(TYPIO_LOG_DEBUG,
                  "Suppressing repeat of startup-guarded key: keycode=%u", key);
        return;
    }

    if (kstate == TYPIO_KEY_APP_SHORTCUT) {
        if ((keyboard->physical_modifiers &
             (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) != 0) {
            key_route_trace(keyboard, "press-forward", key, keysym, modifiers, unicode,
                            kstate, "latched app shortcut");
            typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_PRESSED, unicode);
        }
        typio_log(TYPIO_LOG_DEBUG,
                  "Maintaining latched app shortcut route: keycode=%u", key);
        return;
    }

    if (kstate == TYPIO_KEY_FORWARDED) {
        key_route_trace(keyboard, "press-forward", key, keysym, modifiers, unicode,
                        kstate, "latched forwarded route");
        typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_PRESSED, unicode);
        typio_log(TYPIO_LOG_DEBUG,
                  "Maintaining latched forwarded route: keycode=%u", key);
        return;
    }

    if (!typio_wl_shortcut_chord_is_switch_modifier(keysym) &&
        (keyboard->physical_modifiers & (TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT)) != 0) {
        frontend->shortcut_chord_saw_non_modifier = true;
    }

    key_claim_current_generation(frontend, key);

    suppress_reason = typio_wl_startup_guard_classify_press(
        keyboard->created_at_ms,
        now_ms,
        keysym,
        has_composition,
        keyboard->suppress_stale_keys);

    if (keyboard->suppress_stale_keys &&
        keyboard->startup_suppressed_count == 0 &&
        suppress_reason != TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY) {
        keyboard->suppress_stale_keys = false;
        typio_log(TYPIO_LOG_DEBUG,
                  "Startup key suppression cleared (startup window elapsed)");
    }

    if (suppress_reason != TYPIO_WL_STARTUP_SUPPRESS_NONE) {
        TypioKeyTrackState suppressed_state =
            suppress_reason == TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY
                ? TYPIO_KEY_SUPPRESSED_STARTUP
                : TYPIO_KEY_SUPPRESSED_ENTER;

        key_set_state(frontend, key, suppressed_state);
        if (suppress_reason == TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY)
            keyboard->startup_suppressed_count++;
        key_route_trace(keyboard, "press-suppress", key, keysym, modifiers, unicode,
                        suppressed_state,
                        suppress_reason == TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY
                            ? "startup stale"
                            : "startup enter");
        typio_log(TYPIO_LOG_DEBUG,
                  "Suppressing startup key press: keycode=%u keysym=0x%x reason=%s",
                  key, keysym,
                  suppress_reason == TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY
                      ? "stale"
                      : "enter");
        return;
    }

    if (key_route_is_app_shortcut(keysym, modifiers)) {
        typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_PRESSED, unicode);
        key_set_state(frontend, key, TYPIO_KEY_APP_SHORTCUT);
        key_route_trace(keyboard, "press-forward", key, keysym, modifiers, unicode,
                        TYPIO_KEY_APP_SHORTCUT, "classified app shortcut");
        typio_log(TYPIO_LOG_DEBUG,
                  "Bypassing engine for application shortcut: keycode=%u keysym=0x%x mods=0x%x",
                  key, keysym, modifiers);
        return;
    }

    key_route_maybe_switch_engine_on_modifier_chord(keyboard, keysym, modifiers);

    {
        TypioKeyEvent event = {
            .type      = TYPIO_EVENT_KEY_PRESS,
            .keycode   = key,
            .keysym    = keysym,
            .modifiers = modifiers,
            .unicode   = unicode,
            .time      = time,
            .is_repeat = false,
        };
        bool is_modifier = typio_key_event_is_modifier_only(&event);
        bool handled = typio_input_context_process_key(session->ctx, &event);

        if (!handled || is_modifier) {
            typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_PRESSED, unicode);
            key_set_state(frontend, key, TYPIO_KEY_FORWARDED);
            key_route_trace(keyboard, "press-forward", key, keysym, modifiers, unicode,
                            TYPIO_KEY_FORWARDED,
                            is_modifier ? "modifier passthrough" : "engine not handled");
            typio_log(TYPIO_LOG_DEBUG, "Key forwarded to application");
        } else {
            key_route_trace(keyboard, "press-engine", key, keysym, modifiers, unicode,
                            TYPIO_KEY_IDLE, "engine handled");
            typio_log(TYPIO_LOG_DEBUG, "Key handled by input method");
        }
    }
}

void typio_wl_key_route_process_release(TypioWlKeyboard *keyboard,
                                        TypioWlSession *session,
                                        uint32_t key,
                                        uint32_t keysym,
                                        uint32_t modifiers,
                                        uint32_t unicode,
                                        uint32_t time) {
    TypioWlFrontend *frontend = keyboard->frontend;
    TypioKeyTrackState kstate = key_get_state(frontend, key);
    uint64_t now_ms = key_route_monotonic_ms();

    if (keyboard->repeating && keyboard->repeat_key == key)
        keyboard->repeat_timer_fd >= 0 ? (void)0 : (void)0;

    switch (kstate) {
    case TYPIO_KEY_RELEASED_PENDING:
        key_route_trace(keyboard, "release-consume", key, keysym, modifiers, unicode,
                        kstate, "released pending");
        key_clear_tracking(frontend, key);
        typio_log(TYPIO_LOG_DEBUG,
                  "Consumed physical release for force-released key: keycode=%u",
                  key);
        return;

    case TYPIO_KEY_SUPPRESSED_STARTUP:
        key_route_trace(keyboard, "release-forward", key, keysym, modifiers, unicode,
                        kstate, "startup stale cleanup");
        key_clear_tracking(frontend, key);
        if (keyboard->startup_suppressed_count > 0)
            keyboard->startup_suppressed_count--;
        if (keyboard->suppress_stale_keys &&
            keyboard->startup_suppressed_count == 0) {
            keyboard->suppress_stale_keys = false;
            typio_log(TYPIO_LOG_DEBUG, "Startup key suppression cleared");
        }
        typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_RELEASED, unicode);
        typio_log(TYPIO_LOG_DEBUG,
                  "Forwarding release for startup key: keycode=%u", key);
        return;

    case TYPIO_KEY_SUPPRESSED_ENTER:
        key_route_trace(keyboard, "release-consume", key, keysym, modifiers, unicode,
                        kstate, "suppressed enter");
        key_clear_tracking(frontend, key);
        typio_log(TYPIO_LOG_DEBUG,
                  "Consumed release for suppressed Enter: keycode=%u", key);
        return;

    case TYPIO_KEY_APP_SHORTCUT:
        key_route_trace(keyboard, "release-forward", key, keysym, modifiers, unicode,
                        kstate, "app shortcut release");
        key_clear_tracking(frontend, key);
        typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_RELEASED, unicode);
        typio_log(TYPIO_LOG_DEBUG,
                  "Forwarded application shortcut release: keycode=%u", key);
        return;

    case TYPIO_KEY_FORWARDED:
        key_route_trace(keyboard, "release-forward", key, keysym, modifiers, unicode,
                        kstate, "forwarded release");
        key_clear_tracking(frontend, key);
        break;

    case TYPIO_KEY_IDLE:
        if (!key_owned_by_active_generation(frontend, key)) {
            TypioKeyEvent release_event = {
                .type      = TYPIO_EVENT_KEY_RELEASE,
                .keycode   = key,
                .keysym    = keysym,
                .modifiers = modifiers,
                .unicode   = unicode,
                .time      = time,
                .is_repeat = false,
            };
            bool is_modifier = typio_key_event_is_modifier_only(&release_event);
            bool should_cleanup_stale_release =
                !is_modifier &&
                (typio_wl_startup_guard_should_cleanup_stale_release(
                 keyboard->created_at_ms, now_ms) ||
                 typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
                     keysym,
                     modifiers,
                     keyboard->saw_blocking_modifier));

            if (should_cleanup_stale_release) {
                key_route_trace(keyboard, "release-forward", key, keysym,
                                modifiers, unicode, kstate,
                                "orphan cleanup release");
                typio_wl_vk_forward_key(keyboard, time, key,
                                        WL_KEYBOARD_KEY_STATE_RELEASED,
                                        unicode);
                typio_log(TYPIO_LOG_DEBUG,
                          "Forwarded orphan release for pre-grab key: keycode=%u",
                          key);
                key_clear_tracking(frontend, key);
                return;
            }

            key_route_trace(keyboard, "release-orphan", key, keysym,
                            modifiers, unicode, kstate,
                            "press never reached Typio in this generation");
            typio_log(TYPIO_LOG_DEBUG,
                      "Consumed orphan release: keycode=%u generation=%u active_generation=%u",
                      key, key_get_generation(frontend, key),
                      frontend->active_key_generation);
            key_clear_tracking(frontend, key);
            return;
        }

        key_route_trace(keyboard, "release-engine", key, keysym, modifiers, unicode,
                        kstate, "idle release");
        if (keyboard->suppress_stale_keys &&
            keyboard->startup_suppressed_count == 0) {
            keyboard->suppress_stale_keys = false;
            typio_log(TYPIO_LOG_DEBUG,
                      "Startup key suppression cleared (unseen key released)");
        }
        {
            TypioKeyEvent ev = {
                .type      = TYPIO_EVENT_KEY_RELEASE,
                .keycode   = key,
                .keysym    = keysym,
                .modifiers = modifiers,
                .unicode   = unicode,
                .time      = time,
                .is_repeat = false,
            };
            typio_input_context_process_key(session->ctx, &ev);
        }
        key_clear_tracking(frontend, key);
        return;
    }

    {
        TypioKeyEvent event = {
            .type      = TYPIO_EVENT_KEY_RELEASE,
            .keycode   = key,
            .keysym    = keysym,
            .modifiers = modifiers,
            .unicode   = unicode,
            .time      = time,
            .is_repeat = false,
        };
        typio_input_context_process_key(session->ctx, &event);
    }

    typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_RELEASED, unicode);
}
