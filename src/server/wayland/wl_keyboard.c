/**
 * @file wl_keyboard.c
 * @brief Keyboard grab and XKB handling
 *
 * Key events from the compositor flow through a per-key state machine
 * (TypioKeyTrackState on TypioWlFrontend) that unifies startup-key
 * suppression, forwarded-key tracking, and force-release suppression
 * into a single enum per keycode.
 */


#include "key_arbiter.h"
#include "key_debug.h"
#include "key_tracking_access.h"
#include "monotonic_time.h"
#include "wl_frontend_internal.h"
#include "wl_trace.h"
#include "modifier_policy.h"
#include "key_route.h"
#include "keyboard_repeat.h"
#include "repeat_guard.h"
#include "shortcut_chord.h"
#include "startup_guard.h"
#include "vk_bridge.h"
#include "xkb_modifiers.h"
#include "typio/typio.h"
#include "typio/engine_manager.h"
#include "utils/log.h"
#include "utils/string.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/timerfd.h>

/* ── Helpers ─────────────────────────────────────────────────────── */

static void keyboard_trace_event(TypioWlKeyboard *keyboard,
                                 const char *stage,
                                 uint32_t key,
                                 uint32_t keysym,
                                 uint32_t modifiers,
                                 uint32_t unicode,
                                 TypioKeyTrackState state,
                                 const char *detail);

static void keyboard_trace_event(TypioWlKeyboard *keyboard,
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
                   key_get_generation(keyboard->frontend, key),
                   keyboard->frontend->active_key_generation,
                   unicode_desc,
                   detail ? detail : "-");
}

static uint32_t keyboard_event_modifiers(TypioWlKeyboard *keyboard,
                                         uint32_t keysym,
                                         uint32_t state) {
    return typio_wl_modifier_policy_effective_modifiers(
        keyboard->physical_modifiers,
        typio_wl_xkb_effective_modifiers(keyboard),
        keyboard->frontend->active_generation_owned_keys,
        keysym,
        state);
}

static void keyboard_update_physical_modifier_state(TypioWlKeyboard *keyboard,
                                                    uint32_t keysym,
                                                    uint32_t state) {
    uint32_t bit;

    if (!keyboard)
        return;

    bit = typio_wl_modifier_policy_effective_modifiers(
        TYPIO_MOD_NONE, TYPIO_MOD_NONE, true, keysym,
        WL_KEYBOARD_KEY_STATE_PRESSED);
    bit &= TYPIO_MOD_SHIFT | TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER;
    if (bit == TYPIO_MOD_NONE)
        return;

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
        keyboard->physical_modifiers |= bit;
    else
        keyboard->physical_modifiers &= ~bit;

    if ((keyboard->physical_modifiers &
         (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) != 0) {
        keyboard->saw_blocking_modifier = true;
    }
}

static void keyboard_sync_physical_modifiers(TypioWlKeyboard *keyboard) {
    if (!keyboard || !keyboard->xkb_state)
        return;

    keyboard->physical_modifiers =
        typio_wl_modifier_policy_sync_physical_modifiers(
            keyboard->physical_modifiers,
            typio_wl_xkb_effective_modifiers(keyboard));
}

/* ── Per-key state machine ───────────────────────────────────────── */

static void keyboard_reset_tracking(TypioWlFrontend *frontend) {
    if (!frontend)
        return;

    typio_wl_key_tracking_reset(frontend->key_states,
                                TYPIO_WL_MAX_TRACKED_KEYS);
    typio_wl_key_tracking_reset_generations(frontend->key_generations,
                                            TYPIO_WL_MAX_TRACKED_KEYS);
}

/* ── Keyboard grab event handler declarations ────────────────────── */

static void kb_handle_keymap(void *data,
                             struct zwp_input_method_keyboard_grab_v2 *kb,
                             uint32_t format, int32_t fd, uint32_t size);
static void kb_handle_key(void *data,
                          struct zwp_input_method_keyboard_grab_v2 *kb,
                          uint32_t serial, uint32_t time, uint32_t key,
                          uint32_t state);
static void kb_handle_modifiers(void *data,
                                struct zwp_input_method_keyboard_grab_v2 *kb,
                                uint32_t serial, uint32_t mods_depressed,
                                uint32_t mods_latched, uint32_t mods_locked,
                                uint32_t group);
static void kb_handle_repeat_info(void *data,
                                  struct zwp_input_method_keyboard_grab_v2 *kb,
                                  int32_t rate, int32_t delay);

static const struct zwp_input_method_keyboard_grab_v2_listener keyboard_grab_listener = {
    .keymap = kb_handle_keymap,
    .key = kb_handle_key,
    .modifiers = kb_handle_modifiers,
    .repeat_info = kb_handle_repeat_info,
};

/* ── Keyboard grab lifecycle ─────────────────────────────────────── */

TypioWlKeyboard *typio_wl_keyboard_create(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->input_method)
        return nullptr;

    TypioWlKeyboard *keyboard = calloc(1, sizeof(TypioWlKeyboard));
    if (!keyboard)
        return nullptr;

    frontend->active_key_generation++;
    if (frontend->active_key_generation == 0)
        frontend->active_key_generation = 1;
    frontend->active_generation_owned_keys = false;
    frontend->active_generation_vk_dirty = false;
    keyboard_reset_tracking(frontend);
    keyboard->frontend = frontend;
    keyboard->suppress_stale_keys = true;
    keyboard->created_at_ms = typio_wl_monotonic_ms();
    typio_wl_key_arbiter_init(&keyboard->arbiter);

    keyboard->repeat_timer_fd = timerfd_create(CLOCK_MONOTONIC,
                                               TFD_CLOEXEC | TFD_NONBLOCK);
    if (keyboard->repeat_timer_fd < 0)
        typio_log(TYPIO_LOG_WARNING, "Failed to create repeat timerfd");

    keyboard->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!keyboard->xkb_context) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create XKB context");
        if (keyboard->repeat_timer_fd >= 0)
            close(keyboard->repeat_timer_fd);
        free(keyboard);
        return nullptr;
    }

    keyboard->grab = zwp_input_method_v2_grab_keyboard(frontend->input_method);
    if (!keyboard->grab) {
        typio_log(TYPIO_LOG_ERROR, "Failed to grab keyboard");
        xkb_context_unref(keyboard->xkb_context);
        if (keyboard->repeat_timer_fd >= 0)
            close(keyboard->repeat_timer_fd);
        free(keyboard);
        return nullptr;
    }

    zwp_input_method_keyboard_grab_v2_add_listener(keyboard->grab,
                                                    &keyboard_grab_listener,
                                                    keyboard);

    typio_wl_trace(frontend, "grab", "action=create status=ok");
    typio_log(TYPIO_LOG_INFO, "Keyboard grab created");
    return keyboard;
}

void typio_wl_keyboard_destroy(TypioWlKeyboard *keyboard) {
    if (!keyboard)
        return;

    typio_wl_vk_release_forwarded_keys(keyboard->frontend,
                                       typio_wl_key_tracking_state_name);
    typio_wl_keyboard_release_grab(keyboard);
    typio_wl_keyboard_repeat_stop(keyboard);
    keyboard_reset_tracking(keyboard->frontend);

    if (keyboard->repeat_timer_fd >= 0)
        close(keyboard->repeat_timer_fd);
    if (keyboard->xkb_state)
        xkb_state_unref(keyboard->xkb_state);
    if (keyboard->xkb_keymap)
        xkb_keymap_unref(keyboard->xkb_keymap);
    if (keyboard->xkb_context)
        xkb_context_unref(keyboard->xkb_context);

    typio_wl_trace(keyboard->frontend, "grab", "action=destroy status=ok");
    typio_log(TYPIO_LOG_DEBUG, "Keyboard destroyed");
    free(keyboard);
}

void typio_wl_keyboard_release_grab(TypioWlKeyboard *keyboard) {
    if (keyboard && keyboard->grab) {
        zwp_input_method_keyboard_grab_v2_release(keyboard->grab);
        keyboard->grab = nullptr;
        typio_wl_trace(keyboard->frontend, "grab", "action=release status=ok");
        typio_log(TYPIO_LOG_DEBUG, "Keyboard grab released");
    }
}

/* ── Keyboard grab event handlers ────────────────────────────────── */

static void kb_handle_keymap(void *data,
                             [[maybe_unused]] struct zwp_input_method_keyboard_grab_v2 *kb,
                             uint32_t format, int32_t fd, uint32_t size) {
    TypioWlKeyboard *keyboard = data;

    typio_wl_trace(keyboard ? keyboard->frontend : nullptr,
                   "keymap",
                   "stage=received format=%u size=%u",
                   format, size);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        typio_log(TYPIO_LOG_WARNING, "Unsupported keymap format: %u", format);
        close(fd);
        return;
    }

    char *map_str = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_str == MAP_FAILED) {
        typio_log(TYPIO_LOG_ERROR, "Failed to mmap keymap");
        close(fd);
        return;
    }

    /* Forward keymap to virtual keyboard */
    typio_wl_vk_forward_keymap(keyboard->frontend, format, fd, size);

    close(fd);

    /* Replace keymap and state */
    if (keyboard->xkb_state) {
        xkb_state_unref(keyboard->xkb_state);
        keyboard->xkb_state = nullptr;
    }
    if (keyboard->xkb_keymap) {
        xkb_keymap_unref(keyboard->xkb_keymap);
        keyboard->xkb_keymap = nullptr;
    }

    keyboard->xkb_keymap = xkb_keymap_new_from_string(keyboard->xkb_context,
                                                      map_str,
                                                      XKB_KEYMAP_FORMAT_TEXT_V1,
                                                      XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);

    if (!keyboard->xkb_keymap) {
        typio_log(TYPIO_LOG_ERROR, "Failed to compile keymap");
        return;
    }

    keyboard->xkb_state = xkb_state_new(keyboard->xkb_keymap);
    if (!keyboard->xkb_state) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create XKB state");
        xkb_keymap_unref(keyboard->xkb_keymap);
        keyboard->xkb_keymap = nullptr;
        return;
    }

    keyboard->mod_shift = xkb_keymap_mod_get_index(keyboard->xkb_keymap, XKB_MOD_NAME_SHIFT);
    keyboard->mod_ctrl  = xkb_keymap_mod_get_index(keyboard->xkb_keymap, XKB_MOD_NAME_CTRL);
    keyboard->mod_alt   = xkb_keymap_mod_get_index(keyboard->xkb_keymap, XKB_MOD_NAME_ALT);
    keyboard->mod_super = xkb_keymap_mod_get_index(keyboard->xkb_keymap, XKB_MOD_NAME_LOGO);
    keyboard->mod_caps  = xkb_keymap_mod_get_index(keyboard->xkb_keymap, XKB_MOD_NAME_CAPS);
    keyboard->mod_num   = xkb_keymap_mod_get_index(keyboard->xkb_keymap, XKB_MOD_NAME_NUM);

    typio_log(TYPIO_LOG_INFO, "XKB keymap loaded");
}

/* ── Key press handler ───────────────────────────────────────────── */

void typio_wl_keyboard_process_key_press(TypioWlKeyboard *keyboard,
                                         TypioWlSession *session,
                                         uint32_t key, uint32_t keysym,
                                         uint32_t modifiers, uint32_t unicode,
                                         uint32_t time) {
    TypioKeyTrackState state_before_repeat;

    typio_wl_key_route_process_press(keyboard, session, key, keysym,
                                     modifiers, unicode, time);

    state_before_repeat = key_get_state(keyboard->frontend, key);

    if (keyboard->repeat_rate > 0 && keyboard->xkb_keymap &&
        typio_wl_repeat_should_run_for_state(state_before_repeat) &&
        xkb_keymap_key_repeats(keyboard->xkb_keymap, key + 8)) {
        typio_wl_keyboard_repeat_maybe_start(keyboard, key, time, modifiers);
    }
}

/* ── Key release handler ─────────────────────────────────────────── */

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

/* ── Main key event dispatcher ───────────────────────────────────── */

static void kb_handle_key(void *data,
                          [[maybe_unused]] struct zwp_input_method_keyboard_grab_v2 *kb,
                          [[maybe_unused]] uint32_t serial, uint32_t time, uint32_t key,
                          uint32_t state) {
    TypioWlKeyboard *keyboard = data;
    TypioWlFrontend *frontend = keyboard->frontend;

    if (!keyboard->xkb_state || !frontend->session)
        return;
    if (!typio_wl_lifecycle_phase_allows_key_events(frontend->lifecycle_phase))
        return;

    TypioWlSession *session = frontend->session;
    if (!session->ctx || !typio_input_context_is_focused(session->ctx))
        return;

    /* XKB conversion */
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(keyboard->xkb_state,
                                                     key + 8);
    uint32_t unicode = xkb_state_key_get_utf32(keyboard->xkb_state, key + 8);
    uint32_t modifiers = keyboard_event_modifiers(keyboard, keysym, state);

    keyboard_trace_event(keyboard,
                         state == WL_KEYBOARD_KEY_STATE_PRESSED ? "dispatch-press" : "dispatch-release",
                         key, (uint32_t)keysym, modifiers, unicode,
                         key_get_state(frontend, key),
                         "dispatch");

    /* Update physical modifier state BEFORE the arbiter so it has
     * accurate knowledge of which modifiers are physically held. */
    keyboard_update_physical_modifier_state(keyboard, (uint32_t)keysym, state);

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
        typio_wl_key_arbiter_press(&keyboard->arbiter, keyboard, session,
                                   key, (uint32_t)keysym, modifiers,
                                   unicode, time);
    else
        typio_wl_key_arbiter_release(&keyboard->arbiter, keyboard, session,
                                     key, (uint32_t)keysym, modifiers,
                                     unicode, time);
}

/* ── Modifier event handler ──────────────────────────────────────── */

static void kb_handle_modifiers(void *data,
                                [[maybe_unused]] struct zwp_input_method_keyboard_grab_v2 *kb,
                                [[maybe_unused]] uint32_t serial, uint32_t mods_depressed,
                                uint32_t mods_latched, uint32_t mods_locked,
                                uint32_t group) {
    TypioWlKeyboard *keyboard = data;

    if (!keyboard->xkb_state)
        return;
    if (!typio_wl_lifecycle_phase_allows_modifier_events(
            keyboard->frontend->lifecycle_phase))
        return;

    uint32_t prev_mods = typio_wl_xkb_effective_modifiers(keyboard);

    xkb_state_update_mask(keyboard->xkb_state,
                          mods_depressed, mods_latched, mods_locked,
                          0, 0, group);
    keyboard->mods_depressed = mods_depressed;
    keyboard->mods_latched = mods_latched;
    keyboard->mods_locked = mods_locked;
    keyboard->mods_group = group;
    uint32_t cur_mods = typio_wl_xkb_effective_modifiers(keyboard);
    keyboard_sync_physical_modifiers(keyboard);
    if ((cur_mods & (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) != 0)
        keyboard->saw_blocking_modifier = true;

    /* Cancel IME-side key repeat on blocking modifier transition */
    if (keyboard->repeating &&
        typio_wl_repeat_should_cancel_on_modifier_transition(prev_mods,
                                                             cur_mods)) {
        typio_wl_keyboard_repeat_stop(keyboard);
        typio_log(TYPIO_LOG_DEBUG,
                  "Stopped key repeat after modifier transition");
    }

    /* Forward modifier state to virtual keyboard */
    typio_wl_vk_forward_modifiers(keyboard, mods_depressed, mods_latched,
                                  mods_locked, group);

    typio_wl_trace(keyboard->frontend,
                   "modifiers",
                   "stage=update depressed=0x%x latched=0x%x locked=0x%x group=%u",
                   mods_depressed, mods_latched, mods_locked, group);
}

static void kb_handle_repeat_info(void *data,
                                  [[maybe_unused]] struct zwp_input_method_keyboard_grab_v2 *kb,
                                  int32_t rate, int32_t delay) {
    TypioWlKeyboard *keyboard = data;

    keyboard->repeat_rate = rate;
    keyboard->repeat_delay = delay;
    typio_wl_trace(keyboard->frontend,
                   "repeat",
                   "stage=info rate=%d delay_ms=%d",
                   rate, delay);
}
