/**
 * @file wl_keyboard.c
 * @brief Keyboard grab and XKB handling
 */

#define _POSIX_C_SOURCE 200809L

#include "wl_frontend_internal.h"
#include "repeat_guard.h"
#include "startup_guard.h"
#include "typio/typio.h"
#include "typio/engine_manager.h"
#include "utils/log.h"
#include "utils/string.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/timerfd.h>

#define TYPIO_ENGINE_TOGGLE_THRESHOLD_MS 1000

/* Forward declarations */
static uint32_t xkb_to_typio_modifiers(TypioWlKeyboard *keyboard);
static uint32_t keyboard_modifiers_for_event(uint32_t modifiers,
                                             uint32_t keysym,
                                             uint32_t state);
static void keyboard_forward_key(TypioWlKeyboard *keyboard, uint32_t time,
                                  uint32_t key, uint32_t state);
static bool keyboard_is_tracked_key(uint32_t key);
static void keyboard_track_forwarded_key(TypioWlFrontend *frontend,
                                         uint32_t key,
                                         uint32_t state);
static bool keyboard_consume_suppressed_release(TypioWlFrontend *frontend,
                                                uint32_t key);

/* Keyboard grab event handlers */
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

/* (XKB modifier indices stored per-keyboard in TypioWlKeyboard) */

static uint64_t keyboard_monotonic_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

static bool keyboard_should_repeat(uint32_t modifiers) {
    return (modifiers & (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) == 0;
}

static uint32_t keyboard_modifiers_for_event(uint32_t modifiers,
                                             uint32_t keysym,
                                             uint32_t state) {
    uint32_t bit = TYPIO_MOD_NONE;

    switch (keysym) {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
        bit = TYPIO_MOD_SHIFT;
        break;
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
        bit = TYPIO_MOD_CTRL;
        break;
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
        bit = TYPIO_MOD_ALT;
        break;
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
        bit = TYPIO_MOD_SUPER;
        break;
    default:
        return modifiers;
    }

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        return modifiers | bit;
    }

    return modifiers & ~bit;
}

static bool keyboard_is_tracked_key(uint32_t key) {
    return key < TYPIO_WL_MAX_TRACKED_KEYS;
}

static void keyboard_track_forwarded_key(TypioWlFrontend *frontend,
                                         uint32_t key,
                                         uint32_t state) {
    if (!frontend || !keyboard_is_tracked_key(key)) {
        return;
    }

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        frontend->forwarded_keys[key] = true;
        frontend->suppressed_forwarded_releases[key] = false;
        return;
    }

    frontend->forwarded_keys[key] = false;
    frontend->suppressed_forwarded_releases[key] = false;
}

static bool keyboard_consume_suppressed_release(TypioWlFrontend *frontend,
                                                uint32_t key) {
    if (!frontend || !keyboard_is_tracked_key(key) ||
        !frontend->suppressed_forwarded_releases[key]) {
        return false;
    }

    frontend->suppressed_forwarded_releases[key] = false;
    return true;
}

static void keyboard_repeat_start(TypioWlKeyboard *keyboard, uint32_t key,
                                   uint32_t time, uint32_t modifiers) {
    struct itimerspec its;

    if (keyboard->repeat_timer_fd < 0 || keyboard->repeat_rate <= 0 ||
        !keyboard_should_repeat(modifiers)) {
        return;
    }

    keyboard->repeat_key = key;
    keyboard->repeat_time = time;
    keyboard->repeating = true;

    /* Initial delay, then interval at repeat_rate */
    long delay_ms = keyboard->repeat_delay > 0 ? keyboard->repeat_delay : 600;
    long interval_ms = 1000 / keyboard->repeat_rate;
    if (interval_ms < 1) {
        interval_ms = 1;
    }

    its.it_value.tv_sec = delay_ms / 1000;
    its.it_value.tv_nsec = (delay_ms % 1000) * 1000000L;
    its.it_interval.tv_sec = interval_ms / 1000;
    its.it_interval.tv_nsec = (interval_ms % 1000) * 1000000L;

    timerfd_settime(keyboard->repeat_timer_fd, 0, &its, NULL);
}

static void keyboard_repeat_stop(TypioWlKeyboard *keyboard) {
    struct itimerspec its = {0};

    if (keyboard->repeat_timer_fd >= 0) {
        timerfd_settime(keyboard->repeat_timer_fd, 0, &its, NULL);
    }
    keyboard->repeating = false;
}

void typio_wl_keyboard_cancel_repeat(TypioWlKeyboard *keyboard) {
    if (!keyboard || !keyboard->repeating) {
        return;
    }

    keyboard_repeat_stop(keyboard);
}

void typio_wl_keyboard_release_forwarded_keys(TypioWlFrontend *frontend) {
    uint32_t time;
    size_t key;

    if (!frontend || !frontend->virtual_keyboard ||
        !frontend->virtual_keyboard_has_keymap) {
        return;
    }

    time = (uint32_t)keyboard_monotonic_ms();

    for (key = 0; key < TYPIO_WL_MAX_TRACKED_KEYS; key++) {
        if (!frontend->forwarded_keys[key]) {
            continue;
        }

        zwp_virtual_keyboard_v1_key(frontend->virtual_keyboard,
                                    time,
                                    (uint32_t)key,
                                    WL_KEYBOARD_KEY_STATE_RELEASED);
        frontend->forwarded_keys[key] = false;
        frontend->suppressed_forwarded_releases[key] = true;
    }
}

int typio_wl_keyboard_get_repeat_fd(TypioWlKeyboard *keyboard) {
    if (!keyboard) {
        return -1;
    }
    return keyboard->repeat_timer_fd;
}

void typio_wl_keyboard_dispatch_repeat(TypioWlKeyboard *keyboard) {
    uint64_t expirations;
    xkb_keycode_t xkb_keycode;
    xkb_keysym_t keysym;
    uint32_t unicode;
    uint32_t modifiers;
    TypioWlSession *session;

    if (!keyboard || !keyboard->repeating || keyboard->repeat_timer_fd < 0) {
        return;
    }

    /* Consume the timer expiration */
    if (read(keyboard->repeat_timer_fd, &expirations, sizeof(expirations)) < 0) {
        return;
    }

    if (!keyboard->xkb_state || !keyboard->frontend->session) {
        return;
    }

    session = keyboard->frontend->session;
    if (!session->ctx || !typio_input_context_is_focused(session->ctx)) {
        return;
    }

    xkb_keycode = keyboard->repeat_key + 8;
    keysym = xkb_state_key_get_one_sym(keyboard->xkb_state, xkb_keycode);
    unicode = xkb_state_key_get_utf32(keyboard->xkb_state, xkb_keycode);
    modifiers = xkb_to_typio_modifiers(keyboard);

    TypioKeyEvent event = {
        .type = TYPIO_EVENT_KEY_PRESS,
        .keycode = keyboard->repeat_key,
        .keysym = keysym,
        .modifiers = modifiers,
        .unicode = unicode,
        .time = keyboard->repeat_time,
        .is_repeat = true,
    };

    bool handled = typio_input_context_process_key(session->ctx, &event);
    if (!handled) {
        keyboard_forward_key(keyboard, keyboard->repeat_time,
                             keyboard->repeat_key,
                             WL_KEYBOARD_KEY_STATE_PRESSED);
    }
}

TypioWlKeyboard *typio_wl_keyboard_create(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->input_method) {
        return NULL;
    }

    TypioWlKeyboard *keyboard = calloc(1, sizeof(TypioWlKeyboard));
    if (!keyboard) {
        return NULL;
    }

    keyboard->frontend = frontend;
    keyboard->suppress_stale_keys = true;
    keyboard->created_at_ms = keyboard_monotonic_ms();

    /* Create repeat timer */
    keyboard->repeat_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (keyboard->repeat_timer_fd < 0) {
        typio_log(TYPIO_LOG_WARNING, "Failed to create repeat timerfd");
    }

    /* Create XKB context */
    keyboard->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!keyboard->xkb_context) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create XKB context");
        free(keyboard);
        return NULL;
    }

    /* Request keyboard grab */
    keyboard->grab = zwp_input_method_v2_grab_keyboard(frontend->input_method);
    if (!keyboard->grab) {
        typio_log(TYPIO_LOG_ERROR, "Failed to grab keyboard");
        xkb_context_unref(keyboard->xkb_context);
        free(keyboard);
        return NULL;
    }

    zwp_input_method_keyboard_grab_v2_add_listener(keyboard->grab,
                                                    &keyboard_grab_listener,
                                                    keyboard);

    typio_log(TYPIO_LOG_INFO, "Keyboard grab created");
    return keyboard;
}

void typio_wl_keyboard_destroy(TypioWlKeyboard *keyboard) {
    if (!keyboard) {
        return;
    }

    typio_wl_keyboard_release_forwarded_keys(keyboard->frontend);
    typio_wl_keyboard_release_grab(keyboard);
    keyboard_repeat_stop(keyboard);

    if (keyboard->repeat_timer_fd >= 0) {
        close(keyboard->repeat_timer_fd);
    }

    if (keyboard->xkb_state) {
        xkb_state_unref(keyboard->xkb_state);
    }
    if (keyboard->xkb_keymap) {
        xkb_keymap_unref(keyboard->xkb_keymap);
    }
    if (keyboard->xkb_context) {
        xkb_context_unref(keyboard->xkb_context);
    }

    free(keyboard->prev_engine_name);
    typio_log(TYPIO_LOG_DEBUG, "Keyboard destroyed");
    free(keyboard);
}

/**
 * Switch engine on Ctrl+Shift release.
 *
 * If the interval since the last switch exceeds TYPIO_ENGINE_TOGGLE_THRESHOLD_MS,
 * toggle between the current engine and the previous one.
 * Otherwise, cycle through all engines in order.
 */
static void keyboard_switch_engine(TypioWlKeyboard *keyboard) {
    TypioWlFrontend *frontend = keyboard->frontend;
    TypioEngineManager *manager = typio_instance_get_engine_manager(frontend->instance);
    if (!manager) {
        return;
    }

    TypioEngine *current = typio_engine_manager_get_active(manager);
    const char *current_name = current ? typio_engine_get_name(current) : NULL;

    uint64_t now = keyboard_monotonic_ms();
    uint64_t elapsed = now - keyboard->last_engine_switch_ms;

    if (elapsed > TYPIO_ENGINE_TOGGLE_THRESHOLD_MS &&
        keyboard->prev_engine_name && current_name) {
        /* Toggle mode: switch to previous engine */
        char *target = keyboard->prev_engine_name;
        keyboard->prev_engine_name = typio_strdup(current_name);

        typio_log_info("Engine toggle: %s -> %s", current_name, target);
        typio_engine_manager_set_active(manager, target);
        free(target);
    } else {
        /* Cycle mode: go to next engine */
        free(keyboard->prev_engine_name);
        keyboard->prev_engine_name = current_name ? typio_strdup(current_name) : NULL;

        typio_engine_manager_next(manager);

        TypioEngine *new_engine = typio_engine_manager_get_active(manager);
        const char *new_name = new_engine ? typio_engine_get_name(new_engine) : NULL;
        typio_log_info("Engine cycle: %s -> %s",
                       current_name ? current_name : "(none)",
                       new_name ? new_name : "(none)");
    }

    keyboard->last_engine_switch_ms = now;

    /* Notify frontend about engine change (updates tray icon, etc.) */
    if (frontend->engine_change_callback) {
        frontend->engine_change_callback(frontend->engine_change_user_data);
    }
}

void typio_wl_keyboard_release_grab(TypioWlKeyboard *keyboard) {
    if (keyboard && keyboard->grab) {
        zwp_input_method_keyboard_grab_v2_release(keyboard->grab);
        keyboard->grab = NULL;
        typio_log(TYPIO_LOG_DEBUG, "Keyboard grab released");
    }
}

/* Convert XKB modifiers to Typio modifiers */
static uint32_t xkb_to_typio_modifiers(TypioWlKeyboard *keyboard) {
    struct xkb_state *state = keyboard->xkb_state;
    uint32_t mods = TYPIO_MOD_NONE;

    if (xkb_state_mod_index_is_active(state, keyboard->mod_shift, XKB_STATE_MODS_EFFECTIVE)) {
        mods |= TYPIO_MOD_SHIFT;
    }
    if (xkb_state_mod_index_is_active(state, keyboard->mod_ctrl, XKB_STATE_MODS_EFFECTIVE)) {
        mods |= TYPIO_MOD_CTRL;
    }
    if (xkb_state_mod_index_is_active(state, keyboard->mod_alt, XKB_STATE_MODS_EFFECTIVE)) {
        mods |= TYPIO_MOD_ALT;
    }
    if (xkb_state_mod_index_is_active(state, keyboard->mod_super, XKB_STATE_MODS_EFFECTIVE)) {
        mods |= TYPIO_MOD_SUPER;
    }
    if (xkb_state_mod_index_is_active(state, keyboard->mod_caps, XKB_STATE_MODS_EFFECTIVE)) {
        mods |= TYPIO_MOD_CAPSLOCK;
    }
    if (xkb_state_mod_index_is_active(state, keyboard->mod_num, XKB_STATE_MODS_EFFECTIVE)) {
        mods |= TYPIO_MOD_NUMLOCK;
    }

    return mods;
}

/* Forward an unhandled key event to the application via virtual keyboard */
static void keyboard_forward_key(TypioWlKeyboard *keyboard, uint32_t time,
                                 uint32_t key, uint32_t state) {
    TypioWlFrontend *frontend = keyboard->frontend;
    if (!frontend->virtual_keyboard || !frontend->virtual_keyboard_has_keymap) {
        return;
    }
    zwp_virtual_keyboard_v1_key(frontend->virtual_keyboard, time, key, state);
    keyboard_track_forwarded_key(frontend, key, state);
}

static void keyboard_forward_modifiers(TypioWlKeyboard *keyboard,
                                       uint32_t mods_depressed,
                                       uint32_t mods_latched,
                                       uint32_t mods_locked,
                                       uint32_t group) {
    TypioWlFrontend *frontend = keyboard->frontend;
    if (!frontend->virtual_keyboard || !frontend->virtual_keyboard_has_keymap) {
        return;
    }
    zwp_virtual_keyboard_v1_modifiers(frontend->virtual_keyboard,
                                      mods_depressed, mods_latched,
                                      mods_locked, group);
}

/* Keyboard grab handlers */
static void kb_handle_keymap(void *data,
                             struct zwp_input_method_keyboard_grab_v2 *kb,
                             uint32_t format, int32_t fd, uint32_t size) {
    TypioWlKeyboard *keyboard = data;
    (void)kb;

    typio_log(TYPIO_LOG_DEBUG, "Keymap received: format=%u, size=%u", format, size);

    /* Only support XKB_V1 format */
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        typio_log(TYPIO_LOG_WARNING, "Unsupported keymap format: %u", format);
        close(fd);
        return;
    }

    /* Memory map the keymap */
    char *map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (map_str == MAP_FAILED) {
        typio_log(TYPIO_LOG_ERROR, "Failed to mmap keymap");
        close(fd);
        return;
    }

    /* Forward keymap to virtual keyboard for key forwarding */
    if (keyboard->frontend->virtual_keyboard) {
        /* Create a new fd for the virtual keyboard (it takes ownership) */
        int vk_fd = dup(fd);
        if (vk_fd >= 0) {
            zwp_virtual_keyboard_v1_keymap(keyboard->frontend->virtual_keyboard,
                                           format, vk_fd, size);
            keyboard->frontend->virtual_keyboard_has_keymap = true;
            typio_log(TYPIO_LOG_DEBUG, "Forwarded keymap to virtual keyboard");
        }
    }

    close(fd);

    /* Clean up old keymap/state */
    if (keyboard->xkb_state) {
        xkb_state_unref(keyboard->xkb_state);
        keyboard->xkb_state = NULL;
    }
    if (keyboard->xkb_keymap) {
        xkb_keymap_unref(keyboard->xkb_keymap);
        keyboard->xkb_keymap = NULL;
    }

    /* Create new keymap */
    keyboard->xkb_keymap = xkb_keymap_new_from_string(keyboard->xkb_context,
                                                      map_str,
                                                      XKB_KEYMAP_FORMAT_TEXT_V1,
                                                      XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);

    if (!keyboard->xkb_keymap) {
        typio_log(TYPIO_LOG_ERROR, "Failed to compile keymap");
        return;
    }

    /* Create state */
    keyboard->xkb_state = xkb_state_new(keyboard->xkb_keymap);
    if (!keyboard->xkb_state) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create XKB state");
        xkb_keymap_unref(keyboard->xkb_keymap);
        keyboard->xkb_keymap = NULL;
        return;
    }

    /* Get modifier indices (stored per-keyboard) */
    keyboard->mod_shift = xkb_keymap_mod_get_index(keyboard->xkb_keymap, XKB_MOD_NAME_SHIFT);
    keyboard->mod_ctrl = xkb_keymap_mod_get_index(keyboard->xkb_keymap, XKB_MOD_NAME_CTRL);
    keyboard->mod_alt = xkb_keymap_mod_get_index(keyboard->xkb_keymap, XKB_MOD_NAME_ALT);
    keyboard->mod_super = xkb_keymap_mod_get_index(keyboard->xkb_keymap, XKB_MOD_NAME_LOGO);
    keyboard->mod_caps = xkb_keymap_mod_get_index(keyboard->xkb_keymap, XKB_MOD_NAME_CAPS);
    keyboard->mod_num = xkb_keymap_mod_get_index(keyboard->xkb_keymap, XKB_MOD_NAME_NUM);

    typio_log(TYPIO_LOG_INFO, "XKB keymap loaded");
}

static void kb_handle_key(void *data,
                          struct zwp_input_method_keyboard_grab_v2 *kb,
                          uint32_t serial, uint32_t time, uint32_t key,
                          uint32_t state) {
    TypioWlKeyboard *keyboard = data;
    TypioWlFrontend *frontend = keyboard->frontend;
    bool was_suppressed_startup_key;
    (void)kb;
    (void)serial;

    if (!keyboard->xkb_state) {
        return;
    }

    if (state == WL_KEYBOARD_KEY_STATE_RELEASED &&
        keyboard_consume_suppressed_release(frontend, key)) {
        typio_log(TYPIO_LOG_DEBUG,
                  "Suppressing physical release for forced-released key: keycode=%u",
                  key);
        if (keyboard->repeating && keyboard->repeat_key == key) {
            keyboard_repeat_stop(keyboard);
        }
        return;
    }

    if (!frontend->session) {
        return;
    }

    TypioWlSession *session = frontend->session;
    if (!session->ctx || !typio_input_context_is_focused(session->ctx)) {
        return;
    }

    /* Convert evdev keycode to XKB keycode (+8) */
    xkb_keycode_t xkb_keycode = key + 8;

    /* Get keysym */
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(keyboard->xkb_state, xkb_keycode);

    /* Get Unicode character */
    uint32_t unicode = xkb_state_key_get_utf32(keyboard->xkb_state, xkb_keycode);

    /* Get modifiers from XKB state, then fold in the current modifier key's
     * own transition because the compositor updates xkb_state separately. */
    uint32_t modifiers = xkb_to_typio_modifiers(keyboard);
    uint32_t event_modifiers = keyboard_modifiers_for_event(modifiers, keysym, state);

    typio_log(TYPIO_LOG_DEBUG, "Key %s: keycode=%u, keysym=0x%x, unicode=0x%x, mods=0x%x",
              state == WL_KEYBOARD_KEY_STATE_PRESSED ? "press" : "release",
              key, keysym, unicode, event_modifiers);

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED &&
        typio_wl_startup_guard_track_press(keyboard->suppressed_keys,
                                           TYPIO_WL_MAX_TRACKED_KEYS,
                                           &keyboard->suppressed_key_count,
                                           keyboard->suppress_stale_keys,
                                           keyboard->created_at_ms,
                                           keyboard_monotonic_ms(),
                                           key,
                                           keysym)) {
        if (typio_wl_startup_guard_should_ignore_enter(
                keyboard->created_at_ms, keyboard_monotonic_ms(), keysym)) {
            typio_log(TYPIO_LOG_DEBUG,
                      "Suppressing startup Enter until release: keycode=%u keysym=0x%x",
                      key, keysym);
        } else {
            typio_log(TYPIO_LOG_DEBUG,
                      "Suppressing stale startup key press: keycode=%u", key);
        }
        return;
    }

    was_suppressed_startup_key =
        state == WL_KEYBOARD_KEY_STATE_RELEASED &&
        key < TYPIO_WL_MAX_TRACKED_KEYS &&
        keyboard->suppressed_keys[key];

    if (state == WL_KEYBOARD_KEY_STATE_RELEASED &&
        typio_wl_startup_guard_track_release(keyboard->suppressed_keys,
                                             TYPIO_WL_MAX_TRACKED_KEYS,
                                             &keyboard->suppressed_key_count,
                                             &keyboard->suppress_stale_keys,
                                             key)) {
        if (!keyboard->suppress_stale_keys) {
            typio_log(TYPIO_LOG_DEBUG, "Startup key suppression cleared");
        }
        if (!was_suppressed_startup_key ||
            key >= TYPIO_WL_MAX_TRACKED_KEYS ||
            !frontend->forwarded_keys[key]) {
            return;
        }

        typio_log(TYPIO_LOG_DEBUG,
                  "Forwarding release for stale key that remained logically pressed: keycode=%u",
                  key);
    }

    /* Handle key releases */
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        /* Stop repeating if this key was repeating */
        if (keyboard->repeating && keyboard->repeat_key == key) {
            keyboard_repeat_stop(keyboard);
        }

        /* Send release to engine (needed for Rime Shift toggle etc.) */
        TypioKeyEvent release_event = {
            .type = TYPIO_EVENT_KEY_RELEASE,
            .keycode = key,
            .keysym = keysym,
            .modifiers = event_modifiers,
            .unicode = unicode,
            .time = time,
            .is_repeat = false,
        };
        typio_input_context_process_key(session->ctx, &release_event);

        keyboard_forward_key(keyboard, time, key, state);
        return;
    }

    /* Disarm Ctrl+Shift engine switch if a non-modifier key is pressed */
    if (keyboard->engine_switch_armed) {
        switch (keysym) {
        case XKB_KEY_Shift_L: case XKB_KEY_Shift_R:
        case XKB_KEY_Control_L: case XKB_KEY_Control_R:
            break;
        default:
            keyboard->engine_switch_armed = false;
            break;
        }
    }

    /* Create TypioKeyEvent */
    TypioKeyEvent event = {
        .type = TYPIO_EVENT_KEY_PRESS,
        .keycode = key,
        .keysym = keysym,
        .modifiers = event_modifiers,
        .unicode = unicode,
        .time = time,
        .is_repeat = false,
    };

    /* Process through Typio */
    bool handled = typio_input_context_process_key(session->ctx, &event);

    if (!handled) {
        /* Forward unhandled keys to the application via virtual keyboard */
        keyboard_forward_key(keyboard, time, key, state);
        typio_log(TYPIO_LOG_DEBUG, "Key forwarded to application");
    } else {
        typio_log(TYPIO_LOG_DEBUG, "Key handled by input method");
    }

    /* Start key repeat if the key repeats according to XKB */
    if (keyboard->repeat_rate > 0 && keyboard->xkb_keymap &&
        xkb_keymap_key_repeats(keyboard->xkb_keymap, xkb_keycode)) {
        keyboard_repeat_start(keyboard, key, time, event_modifiers);
    }
}

static void kb_handle_modifiers(void *data,
                                struct zwp_input_method_keyboard_grab_v2 *kb,
                                uint32_t serial, uint32_t mods_depressed,
                                uint32_t mods_latched, uint32_t mods_locked,
                                uint32_t group) {
    TypioWlKeyboard *keyboard = data;
    uint32_t previous_modifiers;
    uint32_t current_modifiers;
    (void)kb;
    (void)serial;

    if (!keyboard->xkb_state) {
        return;
    }

    /* Check Ctrl+Shift state BEFORE updating xkb_state */
    bool was_ctrl_shift = keyboard->engine_switch_armed;
    previous_modifiers = xkb_to_typio_modifiers(keyboard);

    xkb_state_update_mask(keyboard->xkb_state,
                          mods_depressed, mods_latched, mods_locked,
                          0, 0, group);
    current_modifiers = xkb_to_typio_modifiers(keyboard);

    if (keyboard->repeating &&
        typio_wl_repeat_should_cancel_on_modifier_transition(
            previous_modifiers, current_modifiers)) {
        keyboard_repeat_stop(keyboard);
        typio_log(TYPIO_LOG_DEBUG,
                  "Stopped key repeat after blocking modifier transition");
    }

    /* Check if Ctrl+Shift are both currently pressed (and nothing else) */
    bool ctrl_active = xkb_state_mod_index_is_active(keyboard->xkb_state,
        keyboard->mod_ctrl, XKB_STATE_MODS_DEPRESSED);
    bool shift_active = xkb_state_mod_index_is_active(keyboard->xkb_state,
        keyboard->mod_shift, XKB_STATE_MODS_DEPRESSED);
    bool alt_active = xkb_state_mod_index_is_active(keyboard->xkb_state,
        keyboard->mod_alt, XKB_STATE_MODS_DEPRESSED);
    bool super_active = xkb_state_mod_index_is_active(keyboard->xkb_state,
        keyboard->mod_super, XKB_STATE_MODS_DEPRESSED);

    bool ctrl_shift_now = ctrl_active && shift_active && !alt_active && !super_active;

    if (ctrl_shift_now && !was_ctrl_shift) {
        /* Ctrl+Shift just became active — arm */
        keyboard->engine_switch_armed = true;
    } else if (!ctrl_shift_now && was_ctrl_shift) {
        /* Ctrl+Shift released — fire if still armed */
        keyboard->engine_switch_armed = false;
        keyboard_switch_engine(keyboard);
    }

    /* Forward modifier state to virtual keyboard */
    keyboard_forward_modifiers(keyboard, mods_depressed, mods_latched,
                               mods_locked, group);

    typio_log(TYPIO_LOG_DEBUG, "Modifiers: depressed=0x%x, latched=0x%x, locked=0x%x, group=%u",
              mods_depressed, mods_latched, mods_locked, group);
}

static void kb_handle_repeat_info(void *data,
                                  struct zwp_input_method_keyboard_grab_v2 *kb,
                                  int32_t rate, int32_t delay) {
    TypioWlKeyboard *keyboard = data;
    (void)kb;

    keyboard->repeat_rate = rate;
    keyboard->repeat_delay = delay;

    typio_log(TYPIO_LOG_DEBUG, "Repeat info: rate=%d/s, delay=%dms", rate, delay);
}
