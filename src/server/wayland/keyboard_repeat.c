/**
 * @file keyboard_repeat.c
 * @brief Keyboard repeat helpers for Wayland keyboard grabs
 */

#define _POSIX_C_SOURCE 200809L

#include "keyboard_repeat.h"

#include "key_debug.h"
#include "repeat_guard.h"
#include "vk_bridge.h"
#include "wl_frontend_internal.h"
#include "wl_trace.h"
#include "typio/typio.h"
#include "utils/log.h"

#include <sys/timerfd.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

static uint32_t keyboard_repeat_xkb_modifiers(TypioWlKeyboard *keyboard) {
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

static TypioKeyTrackState keyboard_repeat_key_state(TypioWlFrontend *frontend,
                                                    uint32_t key) {
    return key < TYPIO_WL_MAX_TRACKED_KEYS
        ? frontend->key_states[key]
        : TYPIO_KEY_IDLE;
}

static bool keyboard_repeat_should_run(uint32_t modifiers) {
    return (modifiers & (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) == 0;
}

static void keyboard_repeat_trace(TypioWlKeyboard *keyboard,
                                  const char *stage,
                                  uint32_t key,
                                  uint32_t keysym,
                                  uint32_t modifiers,
                                  uint32_t unicode,
                                  TypioKeyTrackState state,
                                  const char *detail) {
    uint32_t xkb_mods = 0;
    const char *route = "unknown";
    char keysym_desc[64];
    char unicode_desc[64];

    if (!keyboard)
        return;

    if (keyboard->xkb_state)
        xkb_mods = keyboard_repeat_xkb_modifiers(keyboard);
    typio_wl_key_debug_format_keysym(keysym, keysym_desc, sizeof(keysym_desc));
    typio_wl_key_debug_format(unicode, unicode_desc, sizeof(unicode_desc));

    switch (state) {
    case TYPIO_KEY_IDLE:
        route = "idle";
        break;
    case TYPIO_KEY_FORWARDED:
        route = "forwarded";
        break;
    case TYPIO_KEY_APP_SHORTCUT:
        route = "app_shortcut";
        break;
    case TYPIO_KEY_RELEASED_PENDING:
        route = "released_pending";
        break;
    case TYPIO_KEY_SUPPRESSED_STARTUP:
        route = "suppressed_startup";
        break;
    }

    typio_wl_trace(keyboard->frontend,
                   "key",
                   "stage=%s keycode=%u keysym=0x%x %s route=%s mods=0x%x phys=0x%x xkb=0x%x keygen=%u activegen=%u %s detail=%s",
                   stage ? stage : "unknown",
                   key,
                   keysym,
                   keysym_desc,
                   route,
                   modifiers,
                   keyboard->physical_modifiers,
                   xkb_mods,
                   key < TYPIO_WL_MAX_TRACKED_KEYS ? keyboard->frontend->key_generations[key] : 0,
                   keyboard->frontend->active_key_generation,
                   unicode_desc,
                   detail ? detail : "-");
}

void typio_wl_keyboard_repeat_maybe_start(TypioWlKeyboard *keyboard,
                                          unsigned int key,
                                          unsigned int time,
                                          unsigned int modifiers) {
    long delay_ms;
    long interval_ms;
    struct itimerspec its;

    if (!keyboard || keyboard->repeat_timer_fd < 0 || keyboard->repeat_rate <= 0 ||
        !keyboard_repeat_should_run(modifiers)) {
        return;
    }

    keyboard->repeat_key = key;
    keyboard->repeat_time = time;
    keyboard->repeating = true;

    delay_ms = keyboard->repeat_delay > 0 ? keyboard->repeat_delay : 600;
    interval_ms = 1000 / keyboard->repeat_rate;
    if (interval_ms < 1)
        interval_ms = 1;

    its = (struct itimerspec) {
        .it_value = { delay_ms / 1000, (delay_ms % 1000) * 1000000L },
        .it_interval = { interval_ms / 1000, (interval_ms % 1000) * 1000000L },
    };
    timerfd_settime(keyboard->repeat_timer_fd, 0, &its, NULL);
}

void typio_wl_keyboard_repeat_stop(TypioWlKeyboard *keyboard) {
    struct itimerspec its = {0};

    if (!keyboard)
        return;

    if (keyboard->repeat_timer_fd >= 0)
        timerfd_settime(keyboard->repeat_timer_fd, 0, &its, NULL);
    keyboard->repeating = false;
}

void typio_wl_keyboard_cancel_repeat(TypioWlKeyboard *keyboard) {
    if (keyboard && keyboard->repeating)
        typio_wl_keyboard_repeat_stop(keyboard);
}

int typio_wl_keyboard_get_repeat_fd(TypioWlKeyboard *keyboard) {
    return keyboard ? keyboard->repeat_timer_fd : -1;
}

void typio_wl_keyboard_dispatch_repeat(TypioWlKeyboard *keyboard) {
    TypioKeyTrackState repeat_state;
    uint64_t expirations;
    TypioWlSession *session;
    xkb_keycode_t xkb_keycode;

    if (!keyboard || !keyboard->repeating || keyboard->repeat_timer_fd < 0)
        return;

    if (!typio_wl_lifecycle_phase_allows_key_events(
            keyboard->frontend->lifecycle_phase)) {
        typio_wl_keyboard_repeat_stop(keyboard);
        return;
    }

    if (read(keyboard->repeat_timer_fd, &expirations, sizeof(expirations)) < 0)
        return;

    if (!keyboard->xkb_state || !keyboard->frontend->session)
        return;

    session = keyboard->frontend->session;
    if (!session->ctx || !typio_input_context_is_focused(session->ctx))
        return;

    xkb_keycode = keyboard->repeat_key + 8;
    repeat_state = keyboard_repeat_key_state(keyboard->frontend, keyboard->repeat_key);
    uint32_t repeat_keysym = xkb_state_key_get_one_sym(keyboard->xkb_state,
                                                       xkb_keycode);
    uint32_t repeat_unicode = xkb_state_key_get_utf32(keyboard->xkb_state,
                                                      xkb_keycode);

    if (repeat_state == TYPIO_KEY_APP_SHORTCUT) {
        if ((keyboard->physical_modifiers &
             (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) == 0) {
            keyboard_repeat_trace(keyboard, "repeat-stop", keyboard->repeat_key,
                                  repeat_keysym,
                                  keyboard->physical_modifiers |
                                  (keyboard_repeat_xkb_modifiers(keyboard) &
                                   (TYPIO_MOD_CAPSLOCK | TYPIO_MOD_NUMLOCK)),
                                  repeat_unicode,
                                  repeat_state,
                                  "app shortcut lost blocking modifier");
            typio_wl_keyboard_repeat_stop(keyboard);
            return;
        }

        keyboard_repeat_trace(keyboard, "repeat-forward", keyboard->repeat_key,
                              repeat_keysym,
                              keyboard->physical_modifiers |
                              (keyboard_repeat_xkb_modifiers(keyboard) &
                               (TYPIO_MOD_CAPSLOCK | TYPIO_MOD_NUMLOCK)),
                              repeat_unicode,
                              repeat_state,
                              "latched app shortcut repeat");
        typio_wl_vk_forward_key(keyboard, keyboard->repeat_time,
                                keyboard->repeat_key,
                                WL_KEYBOARD_KEY_STATE_PRESSED,
                                repeat_unicode);
        return;
    }

    if (repeat_state == TYPIO_KEY_FORWARDED) {
        keyboard_repeat_trace(keyboard, "repeat-forward", keyboard->repeat_key,
                              repeat_keysym,
                              keyboard->physical_modifiers |
                              (keyboard_repeat_xkb_modifiers(keyboard) &
                               (TYPIO_MOD_CAPSLOCK | TYPIO_MOD_NUMLOCK)),
                              repeat_unicode,
                              repeat_state,
                              "latched forwarded repeat");
        typio_wl_vk_forward_key(keyboard, keyboard->repeat_time,
                                keyboard->repeat_key,
                                WL_KEYBOARD_KEY_STATE_PRESSED,
                                repeat_unicode);
        return;
    }

    if (!typio_wl_repeat_should_run_for_state(repeat_state)) {
        keyboard_repeat_trace(keyboard, "repeat-stop", keyboard->repeat_key,
                              repeat_keysym,
                              keyboard->physical_modifiers |
                              (keyboard_repeat_xkb_modifiers(keyboard) &
                               (TYPIO_MOD_CAPSLOCK | TYPIO_MOD_NUMLOCK)),
                              repeat_unicode,
                              repeat_state,
                              "repeat disallowed for key state");
        typio_wl_keyboard_repeat_stop(keyboard);
        return;
    }

    {
        TypioKeyEvent event = {
            .type      = TYPIO_EVENT_KEY_PRESS,
            .keycode   = keyboard->repeat_key,
            .keysym    = repeat_keysym,
            .modifiers = keyboard->physical_modifiers |
                         (keyboard_repeat_xkb_modifiers(keyboard) &
                          (TYPIO_MOD_CAPSLOCK | TYPIO_MOD_NUMLOCK)),
            .unicode   = repeat_unicode,
            .time      = keyboard->repeat_time,
            .is_repeat = true,
        };

        if (!typio_input_context_process_key(session->ctx, &event)) {
            keyboard_repeat_trace(keyboard, "repeat-forward", keyboard->repeat_key,
                                  event.keysym, event.modifiers, event.unicode,
                                  TYPIO_KEY_FORWARDED,
                                  "engine not handled repeat");
            typio_wl_vk_forward_key(keyboard, keyboard->repeat_time,
                                    keyboard->repeat_key,
                                    WL_KEYBOARD_KEY_STATE_PRESSED,
                                    event.unicode);
            if (keyboard->repeat_key < TYPIO_WL_MAX_TRACKED_KEYS)
                keyboard->frontend->key_states[keyboard->repeat_key] = TYPIO_KEY_FORWARDED;
        }
    }
}
