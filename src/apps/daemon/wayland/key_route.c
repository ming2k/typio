/**
 * @file key_route.c
 * @brief Key press/release routing for Wayland keyboard events
 */


#include "key_route.h"

#include "boundary_bridge.h"
#include "candidate_guard.h"
#include "key_debug.h"
#include "key_tracking_access.h"
#include "shortcut_config.h"
#include "shortcut_chord.h"
#include "startup_guard.h"
#include "vk_bridge.h"
#include "wl_trace.h"
#include "xkb_modifiers.h"
#include "typio/typio.h"
#include "typio/engine_manager.h"
#include "utils/log.h"

#include <xkbcommon/xkbcommon-keysyms.h>

static bool key_route_is_shift_keysym(uint32_t keysym) {
    return keysym == XKB_KEY_Shift_L || keysym == XKB_KEY_Shift_R;
}

static TypioWlKeyDecision key_route_decision(TypioWlKeyAction action,
                                             TypioWlKeyReason reason) {
    TypioWlKeyDecision decision = {
        .action = action,
        .reason = reason,
    };
    return decision;
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
        xkb_mods = typio_wl_xkb_effective_modifiers(keyboard);
    typio_wl_key_debug_format_keysym(keysym, keysym_desc, sizeof(keysym_desc));
    typio_wl_key_debug_format(unicode, unicode_desc, sizeof(unicode_desc));

    typio_wl_trace(keyboard->frontend,
                   "key",
                   "stage=%s keycode=%u keysym=0x%x %s route=%s mods=0x%x phys=0x%x xkb=0x%x keygen=%u activegen=%u %s detail=%s",
                   stage ? stage : "unknown",
                   key,
                   keysym,
                   keysym_desc,
                   typio_wl_key_tracking_state_name(state),
                   modifiers,
                   keyboard->physical_modifiers,
                   xkb_mods,
                   key < TYPIO_WL_MAX_TRACKED_KEYS ? keyboard->frontend->key_generations[key] : 0,
                   keyboard->frontend->active_key_generation,
                   unicode_desc,
                   detail ? detail : "-");
}

static void key_route_trace_decision(TypioWlKeyboard *keyboard,
                                     const char *stage,
                                     uint32_t key,
                                     uint32_t keysym,
                                     uint32_t modifiers,
                                     uint32_t unicode,
                                     TypioKeyTrackState state,
                                     TypioWlKeyDecision decision,
                                     const char *extra) {
    char detail[192];

    snprintf(detail, sizeof(detail),
             "action=%s reason=%s%s%s",
             typio_wl_key_action_name(decision.action),
             typio_wl_key_reason_name(decision.reason),
             extra && *extra ? " " : "",
             extra && *extra ? extra : "");
    key_route_trace(keyboard, stage, key, keysym, modifiers, unicode,
                    state, detail);
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

const char *typio_wl_key_action_name(TypioWlKeyAction action) {
    switch (action) {
    case TYPIO_WL_KEY_ACTION_FORWARD:
        return "forward";
    case TYPIO_WL_KEY_ACTION_CONSUME:
    default:
        return "consume";
    }
}

const char *typio_wl_key_reason_name(TypioWlKeyReason reason) {
    switch (reason) {
    case TYPIO_WL_KEY_REASON_TYPIO_RESERVED:
        return "typio_reserved";
    case TYPIO_WL_KEY_REASON_APPLICATION_SHORTCUT:
        return "application_shortcut";
    case TYPIO_WL_KEY_REASON_ENGINE_HANDLED:
        return "engine_handled";
    case TYPIO_WL_KEY_REASON_ENGINE_UNHANDLED:
        return "engine_unhandled";
    case TYPIO_WL_KEY_REASON_MODIFIER_PASSTHROUGH:
        return "modifier_passthrough";
    case TYPIO_WL_KEY_REASON_CANDIDATE_NAVIGATION:
        return "candidate_navigation";
    case TYPIO_WL_KEY_REASON_STARTUP_SUPPRESSED:
        return "startup_suppressed";
    case TYPIO_WL_KEY_REASON_RELEASED_PENDING:
        return "released_pending";
    case TYPIO_WL_KEY_REASON_LATCHED_APP_SHORTCUT:
        return "latched_app_shortcut";
    case TYPIO_WL_KEY_REASON_LATCHED_FORWARDED:
        return "latched_forwarded";
    case TYPIO_WL_KEY_REASON_STARTUP_STALE_CLEANUP:
        return "startup_stale_cleanup";
    case TYPIO_WL_KEY_REASON_FORWARDED_RELEASE:
        return "forwarded_release";
    case TYPIO_WL_KEY_REASON_ORPHAN_RELEASE_CLEANUP:
        return "orphan_release_cleanup";
    case TYPIO_WL_KEY_REASON_ORPHAN_RELEASE_CONSUMED:
        return "orphan_release_consumed";
    case TYPIO_WL_KEY_REASON_VOICE_PTT:
        return "voice_ptt";
    case TYPIO_WL_KEY_REASON_VOICE_PTT_UNAVAILABLE:
        return "voice_ptt_unavailable";
    case TYPIO_WL_KEY_REASON_NONE:
    default:
        return "none";
    }
}

const char *typio_wl_reserved_action_name(TypioWlReservedAction action) {
    switch (action) {
    case TYPIO_WL_RESERVED_ACTION_EMERGENCY_EXIT:
        return "emergency_exit";
    case TYPIO_WL_RESERVED_ACTION_VOICE_PTT:
        return "voice_ptt";
    case TYPIO_WL_RESERVED_ACTION_NONE:
    default:
        return "none";
    }
}

bool typio_wl_key_route_binding_matches_press(const TypioShortcutBinding *binding,
                                              uint32_t keysym,
                                              uint32_t modifiers) {
    if (!binding || binding->keysym == 0)
        return false;

    return keysym == binding->keysym &&
           (modifiers & binding->modifiers) == binding->modifiers;
}

TypioWlReservedAction typio_wl_key_route_reserved_action(
    const TypioShortcutConfig *shortcuts,
    uint32_t keysym,
    uint32_t modifiers) {
    if (!shortcuts)
        return TYPIO_WL_RESERVED_ACTION_NONE;

    if (typio_wl_key_route_binding_matches_press(&shortcuts->emergency_exit,
                                                 keysym, modifiers)) {
        return TYPIO_WL_RESERVED_ACTION_EMERGENCY_EXIT;
    }

    if (typio_wl_key_route_binding_matches_press(&shortcuts->voice_ptt,
                                                 keysym, modifiers)) {
        return TYPIO_WL_RESERVED_ACTION_VOICE_PTT;
    }

    return TYPIO_WL_RESERVED_ACTION_NONE;
}

static TypioWlKeyDecision key_route_shortcut_decision(
    const TypioShortcutConfig *shortcuts,
    uint32_t keysym,
    uint32_t modifiers,
    TypioWlReservedAction *reserved_action_out) {
    TypioWlReservedAction reserved_action =
        typio_wl_key_route_reserved_action(shortcuts, keysym, modifiers);

    if (reserved_action_out) {
        *reserved_action_out = reserved_action;
    }

    if (reserved_action != TYPIO_WL_RESERVED_ACTION_NONE) {
        return key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                  TYPIO_WL_KEY_REASON_TYPIO_RESERVED);
    }

    if (key_route_is_app_shortcut(keysym, modifiers)) {
        return key_route_decision(TYPIO_WL_KEY_ACTION_FORWARD,
                                  TYPIO_WL_KEY_REASON_APPLICATION_SHORTCUT);
    }

    return key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                              TYPIO_WL_KEY_REASON_NONE);
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
    TypioWlStartupSuppressReason suppress_reason;
    TypioWlReservedAction reserved_action;
    TypioWlKeyDecision decision;

    if (kstate == TYPIO_KEY_TRACK_RELEASED_PENDING) {
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                      TYPIO_WL_KEY_REASON_RELEASED_PENDING);
        key_route_trace_decision(keyboard, "press-ignore", key, keysym,
                                 modifiers, unicode, kstate, decision, nullptr);
        typio_log(TYPIO_LOG_DEBUG,
                  "Suppressing press for force-released key: keycode=%u", key);
        return;
    }

    if (kstate == TYPIO_KEY_TRACK_SUPPRESSED_STARTUP) {
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                      TYPIO_WL_KEY_REASON_STARTUP_SUPPRESSED);
        key_route_trace_decision(keyboard, "press-ignore", key, keysym,
                                 modifiers, unicode, kstate, decision,
                                 "repeat");
        typio_log(TYPIO_LOG_DEBUG,
                  "Suppressing repeat of startup-guarded key: keycode=%u", key);
        return;
    }

    if (kstate == TYPIO_KEY_TRACK_APP_SHORTCUT) {
        if ((keyboard->physical_modifiers &
             (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) != 0) {
            decision = key_route_decision(TYPIO_WL_KEY_ACTION_FORWARD,
                                          TYPIO_WL_KEY_REASON_LATCHED_APP_SHORTCUT);
            key_route_trace_decision(keyboard, "press-forward", key, keysym,
                                     modifiers, unicode, kstate, decision,
                                     nullptr);
            typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_PRESSED, unicode);
        }
        typio_log(TYPIO_LOG_DEBUG,
                  "Maintaining latched app shortcut route: keycode=%u", key);
        return;
    }

    if (kstate == TYPIO_KEY_TRACK_FORWARDED) {
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_FORWARD,
                                      TYPIO_WL_KEY_REASON_LATCHED_FORWARDED);
        key_route_trace_decision(keyboard, "press-forward", key, keysym,
                                 modifiers, unicode, kstate, decision, nullptr);
        typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_PRESSED, unicode);
        typio_log(TYPIO_LOG_DEBUG,
                  "Maintaining latched forwarded route: keycode=%u", key);
        return;
    }

    key_claim_current_generation(frontend, key);

    suppress_reason = typio_wl_startup_guard_classify_press(
        keyboard->created_at_epoch,
        frontend->dispatch_epoch,
        keyboard->suppress_stale_keys);

    if (keyboard->suppress_stale_keys &&
        keyboard->startup_suppressed_count == 0 &&
        suppress_reason != TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY) {
        keyboard->suppress_stale_keys = false;
        typio_log(TYPIO_LOG_DEBUG,
                  "Startup key suppression cleared (startup window elapsed)");
    }

    if (suppress_reason == TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY) {
        key_set_state(frontend, key, TYPIO_KEY_TRACK_SUPPRESSED_STARTUP);
        keyboard->startup_suppressed_count++;
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                      TYPIO_WL_KEY_REASON_STARTUP_SUPPRESSED);
        key_route_trace_decision(keyboard, "press-suppress", key, keysym,
                                 modifiers, unicode,
                                 TYPIO_KEY_TRACK_SUPPRESSED_STARTUP, decision,
                                 "stale");
        typio_log(TYPIO_LOG_DEBUG,
                  "Suppressing startup key press: keycode=%u keysym=0x%x reason=stale",
                  key, keysym);
        return;
    }

    decision = key_route_shortcut_decision(&frontend->shortcuts,
                                           keysym, modifiers,
                                           &reserved_action);

    if (decision.reason != TYPIO_WL_KEY_REASON_NONE) {
        key_route_trace_decision(
            keyboard, "press-classify", key, keysym, modifiers,
            unicode, kstate, decision,
            decision.reason == TYPIO_WL_KEY_REASON_TYPIO_RESERVED
                ? typio_wl_reserved_action_name(reserved_action)
                : nullptr);
    }

    if (reserved_action == TYPIO_WL_RESERVED_ACTION_EMERGENCY_EXIT) {
        typio_log(TYPIO_LOG_WARNING,
                  "Emergency exit shortcut triggered: keycode=%u keysym=0x%x mods=0x%x",
                  key, keysym, modifiers);
        typio_log_dump_recent_to_configured_path("emergency exit shortcut");
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                      TYPIO_WL_KEY_REASON_TYPIO_RESERVED);
        key_route_trace_decision(keyboard, "press-stop", key, keysym, modifiers,
                                 unicode, TYPIO_KEY_TRACK_IDLE, decision,
                                 "emergency_exit");
        typio_wl_keyboard_release_grab(keyboard);
        typio_wl_frontend_stop(frontend);
        return;
    }

#ifdef HAVE_VOICE
    const TypioShortcutBinding *ptt = &frontend->shortcuts.voice_ptt;
    typio_log(TYPIO_LOG_DEBUG,
              "PTT check: keysym=0x%x mods=0x%x ptt_keysym=0x%x ptt_mods=0x%x voice_available=%s",
              keysym, modifiers, ptt->keysym, ptt->modifiers,
              typio_voice_service_is_available(frontend->voice) ? "yes" : "no");
    if (reserved_action == TYPIO_WL_RESERVED_ACTION_VOICE_PTT &&
        keysym == ptt->keysym &&
        (modifiers & ptt->modifiers) == ptt->modifiers) {
        if (typio_voice_service_is_available(frontend->voice)) {
            typio_voice_service_start(frontend->voice);
            key_set_state(frontend, key, TYPIO_KEY_TRACK_VOICE_PTT);
            typio_wl_set_preedit(frontend, "[Recording...]", 0, 0);
            typio_wl_commit(frontend);
            decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                          TYPIO_WL_KEY_REASON_VOICE_PTT);
            key_route_trace_decision(keyboard, "press-ptt", key, keysym,
                                     modifiers, unicode, TYPIO_KEY_TRACK_VOICE_PTT,
                                     decision, "start");
            typio_log(TYPIO_LOG_DEBUG, "Voice PTT started: keycode=%u", key);
        } else {
            const char *reason =
                typio_voice_service_get_unavail_reason(frontend->voice);
            char hint[160];
            snprintf(hint, sizeof(hint), "[Voice unavailable: %s]",
                     reason ? reason : "unknown");
            key_set_state(frontend, key, TYPIO_KEY_TRACK_VOICE_PTT_UNAVAIL);
            typio_wl_set_preedit(frontend, hint, 0, 0);
            typio_wl_commit(frontend);
            decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                          TYPIO_WL_KEY_REASON_VOICE_PTT_UNAVAILABLE);
            key_route_trace_decision(keyboard, "press-ptt-unavail", key, keysym,
                                     modifiers, unicode,
                                     TYPIO_KEY_TRACK_VOICE_PTT_UNAVAIL,
                                     decision, "not_available");
            typio_log(TYPIO_LOG_WARNING,
                      "Voice PTT pressed but unavailable: %s",
                      reason ? reason : "unknown");
        }
        return;
    }
#endif

    if (decision.reason == TYPIO_WL_KEY_REASON_APPLICATION_SHORTCUT) {
        typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_PRESSED, unicode);
        key_set_state(frontend, key, TYPIO_KEY_TRACK_APP_SHORTCUT);
        key_route_trace_decision(keyboard, "press-forward", key, keysym,
                                 modifiers, unicode, TYPIO_KEY_TRACK_APP_SHORTCUT,
                                 decision, nullptr);
        typio_log(TYPIO_LOG_DEBUG,
                  "Bypassing engine for application shortcut: keycode=%u keysym=0x%x mods=0x%x",
                  key, keysym, modifiers);
        return;
    }

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

        if (is_modifier && key_route_is_shift_keysym(keysym)) {
            TypioEngineManager *manager =
                typio_instance_get_engine_manager(frontend->instance);
            TypioEngine *active_engine =
                manager ? typio_engine_manager_get_active(manager) : nullptr;
            typio_log(TYPIO_LOG_DEBUG,
                      "Shift press diagnostic: handled=%s engine=%s mods=0x%x phys=0x%x xkb=0x%x",
                      handled ? "yes" : "no",
                      active_engine ? typio_engine_get_name(active_engine) : "(none)",
                      modifiers,
                      keyboard->physical_modifiers,
                      typio_wl_xkb_effective_modifiers(keyboard));
        }

        if (!handled &&
            typio_wl_candidate_guard_should_consume(session->ctx, keysym)) {
            decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                          TYPIO_WL_KEY_REASON_CANDIDATE_NAVIGATION);
            key_route_trace_decision(keyboard, "press-engine", key, keysym,
                                     modifiers, unicode, TYPIO_KEY_TRACK_IDLE,
                                     decision, nullptr);
            typio_log(TYPIO_LOG_DEBUG,
                      "Reserved navigation key for candidate UI");
        } else if (!handled || is_modifier) {
            typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_PRESSED, unicode);
            key_set_state(frontend, key, TYPIO_KEY_TRACK_FORWARDED);
            decision = key_route_decision(
                TYPIO_WL_KEY_ACTION_FORWARD,
                is_modifier ? TYPIO_WL_KEY_REASON_MODIFIER_PASSTHROUGH
                            : TYPIO_WL_KEY_REASON_ENGINE_UNHANDLED);
            key_route_trace_decision(keyboard, "press-forward", key, keysym,
                                     modifiers, unicode, TYPIO_KEY_TRACK_FORWARDED,
                                     decision, nullptr);
            typio_log(TYPIO_LOG_DEBUG, "Key forwarded to application");
        } else {
            decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                          TYPIO_WL_KEY_REASON_ENGINE_HANDLED);
            key_route_trace_decision(keyboard, "press-engine", key, keysym,
                                     modifiers, unicode, TYPIO_KEY_TRACK_IDLE,
                                     decision, nullptr);
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
    TypioWlKeyDecision decision;

    if (keyboard->repeating && keyboard->repeat_key == key)
        keyboard->repeat_timer_fd >= 0 ? (void)0 : (void)0;

    switch (kstate) {
    case TYPIO_KEY_TRACK_RELEASED_PENDING:
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                      TYPIO_WL_KEY_REASON_RELEASED_PENDING);
        key_route_trace_decision(keyboard, "release-consume", key, keysym,
                                 modifiers, unicode, kstate, decision, nullptr);
        key_clear_tracking(frontend, key);
        typio_log(TYPIO_LOG_DEBUG,
                  "Consumed physical release for force-released key: keycode=%u",
                  key);
        return;

    case TYPIO_KEY_TRACK_SUPPRESSED_STARTUP:
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_FORWARD,
                                      TYPIO_WL_KEY_REASON_STARTUP_STALE_CLEANUP);
        key_route_trace_decision(keyboard, "release-forward", key, keysym,
                                 modifiers, unicode, kstate, decision, nullptr);
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

    case TYPIO_KEY_TRACK_APP_SHORTCUT:
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_FORWARD,
                                      TYPIO_WL_KEY_REASON_APPLICATION_SHORTCUT);
        key_route_trace_decision(keyboard, "release-forward", key, keysym,
                                 modifiers, unicode, kstate, decision,
                                 "release");
        key_clear_tracking(frontend, key);
        typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_RELEASED, unicode);
        typio_log(TYPIO_LOG_DEBUG,
                  "Forwarded application shortcut release: keycode=%u", key);
        return;

    case TYPIO_KEY_TRACK_VOICE_PTT:
#ifdef HAVE_VOICE
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                      TYPIO_WL_KEY_REASON_VOICE_PTT);
        key_route_trace_decision(keyboard, "release-ptt", key, keysym,
                                 modifiers, unicode, kstate, decision, "stop");
        typio_voice_service_stop(frontend->voice);
        typio_wl_set_preedit(frontend, "[Processing...]", 0, 0);
        typio_wl_commit(frontend);
        key_clear_tracking(frontend, key);
        typio_log(TYPIO_LOG_DEBUG, "Voice PTT released: keycode=%u", key);
        return;
#else
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                      TYPIO_WL_KEY_REASON_VOICE_PTT);
        key_route_trace_decision(keyboard, "release-consume", key, keysym,
                                 modifiers, unicode, kstate, decision,
                                 "unsupported_build");
        key_clear_tracking(frontend, key);
        return;
#endif

    case TYPIO_KEY_TRACK_VOICE_PTT_UNAVAIL:
#ifdef HAVE_VOICE
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                      TYPIO_WL_KEY_REASON_VOICE_PTT_UNAVAILABLE);
        key_route_trace_decision(keyboard, "release-ptt-unavail", key, keysym,
                                 modifiers, unicode, kstate, decision,
                                 "release");
        typio_wl_set_preedit(frontend, "", 0, 0);
        typio_wl_commit(frontend);
        key_clear_tracking(frontend, key);
        typio_log(TYPIO_LOG_DEBUG,
                  "Voice PTT unavail released: keycode=%u", key);
        return;
#else
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                      TYPIO_WL_KEY_REASON_VOICE_PTT_UNAVAILABLE);
        key_route_trace_decision(keyboard, "release-consume", key, keysym,
                                 modifiers, unicode, kstate, decision,
                                 "unsupported_build");
        key_clear_tracking(frontend, key);
        return;
#endif

    case TYPIO_KEY_TRACK_FORWARDED:
        decision = key_route_decision(TYPIO_WL_KEY_ACTION_FORWARD,
                                      TYPIO_WL_KEY_REASON_FORWARDED_RELEASE);
        key_route_trace_decision(keyboard, "release-forward", key, keysym,
                                 modifiers, unicode, kstate, decision, nullptr);
        key_clear_tracking(frontend, key);
        break;

    case TYPIO_KEY_TRACK_IDLE:
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
                (typio_wl_startup_guard_is_in_guard_window(
                 keyboard->created_at_epoch, frontend->dispatch_epoch) ||
                 typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
                     keysym,
                     modifiers,
                     keyboard->saw_blocking_modifier));

            if (should_cleanup_stale_release) {
                decision = key_route_decision(
                    TYPIO_WL_KEY_ACTION_FORWARD,
                    TYPIO_WL_KEY_REASON_ORPHAN_RELEASE_CLEANUP);
                key_route_trace_decision(keyboard, "release-forward", key, keysym,
                                         modifiers, unicode, kstate, decision,
                                         nullptr);
                typio_wl_vk_forward_key(keyboard, time, key,
                                        WL_KEYBOARD_KEY_STATE_RELEASED,
                                        unicode);
                typio_log(TYPIO_LOG_DEBUG,
                          "Forwarded orphan release for pre-grab key: keycode=%u",
                          key);
                key_clear_tracking(frontend, key);
                return;
            }

            decision = key_route_decision(
                TYPIO_WL_KEY_ACTION_CONSUME,
                TYPIO_WL_KEY_REASON_ORPHAN_RELEASE_CONSUMED);
            key_route_trace_decision(keyboard, "release-orphan", key, keysym,
                                     modifiers, unicode, kstate, decision,
                                     "press_never_reached_typio");
            typio_log(TYPIO_LOG_DEBUG,
                      "Consumed orphan release: keycode=%u generation=%u active_generation=%u",
                      key, key_get_generation(frontend, key),
                      frontend->active_key_generation);
            key_clear_tracking(frontend, key);
            return;
        }

        decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                      TYPIO_WL_KEY_REASON_ENGINE_HANDLED);
        key_route_trace_decision(keyboard, "release-engine", key, keysym,
                                 modifiers, unicode, kstate, decision,
                                 "idle_release");
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
            bool handled = typio_input_context_process_key(session->ctx, &ev);
            if (typio_key_event_is_modifier_only(&ev) &&
                key_route_is_shift_keysym(keysym)) {
                TypioEngineManager *manager =
                    typio_instance_get_engine_manager(frontend->instance);
                TypioEngine *active_engine =
                    manager ? typio_engine_manager_get_active(manager) : nullptr;
                typio_log(TYPIO_LOG_DEBUG,
                          "Shift release diagnostic: handled=%s engine=%s mods=0x%x phys=0x%x xkb=0x%x",
                          handled ? "yes" : "no",
                          active_engine ? typio_engine_get_name(active_engine) : "(none)",
                          modifiers,
                          keyboard->physical_modifiers,
                          typio_wl_xkb_effective_modifiers(keyboard));
            }
            if (!handled &&
                typio_wl_candidate_guard_should_consume(session->ctx, keysym)) {
                decision = key_route_decision(
                    TYPIO_WL_KEY_ACTION_CONSUME,
                    TYPIO_WL_KEY_REASON_CANDIDATE_NAVIGATION);
                key_route_trace_decision(keyboard, "release-engine", key, keysym,
                                         modifiers, unicode, kstate, decision,
                                         nullptr);
            }
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
        bool handled = typio_input_context_process_key(session->ctx, &event);
        if (typio_key_event_is_modifier_only(&event) &&
            key_route_is_shift_keysym(keysym)) {
            TypioEngineManager *manager =
                typio_instance_get_engine_manager(frontend->instance);
            TypioEngine *active_engine =
                manager ? typio_engine_manager_get_active(manager) : nullptr;
            typio_log(TYPIO_LOG_DEBUG,
                      "Shift release diagnostic: handled=%s engine=%s mods=0x%x phys=0x%x xkb=0x%x",
                      handled ? "yes" : "no",
                      active_engine ? typio_engine_get_name(active_engine) : "(none)",
                      modifiers,
                      keyboard->physical_modifiers,
                      typio_wl_xkb_effective_modifiers(keyboard));
        }
        if (!handled &&
            typio_wl_candidate_guard_should_consume(session->ctx, keysym)) {
            decision = key_route_decision(TYPIO_WL_KEY_ACTION_CONSUME,
                                          TYPIO_WL_KEY_REASON_CANDIDATE_NAVIGATION);
            key_route_trace_decision(keyboard, "release-engine", key, keysym,
                                     modifiers, unicode, TYPIO_KEY_TRACK_IDLE,
                                     decision, nullptr);
            key_clear_tracking(frontend, key);
            return;
        }
    }

    typio_wl_vk_forward_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_RELEASED, unicode);
}
