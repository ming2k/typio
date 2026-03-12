/**
 * @file startup_guard.c
 * @brief Startup key filtering for freshly activated Wayland keyboard grabs
 */

#include "startup_guard.h"
#include "typio/event.h"

#include <stdbool.h>
#include <stddef.h>

bool typio_wl_startup_guard_should_ignore_enter(uint64_t started_at_ms,
                                                uint64_t now_ms,
                                                uint32_t keysym) {
    if (now_ms < started_at_ms) {
        return false;
    }

    if ((now_ms - started_at_ms) > TYPIO_WL_STARTUP_ENTER_GUARD_MS) {
        return false;
    }

    return keysym == TYPIO_KEY_Return || keysym == TYPIO_KEY_KP_Enter;
}

static bool startup_guard_valid_keycode(size_t suppressed_keys_len,
                                        uint32_t keycode) {
    return keycode < suppressed_keys_len;
}

static void startup_guard_mark_suppressed(bool *suppressed_keys,
                                          size_t suppressed_keys_len,
                                          size_t *suppressed_key_count,
                                          uint32_t keycode) {
    if (!suppressed_keys || !suppressed_key_count ||
        !startup_guard_valid_keycode(suppressed_keys_len, keycode) ||
        suppressed_keys[keycode]) {
        return;
    }

    suppressed_keys[keycode] = true;
    (*suppressed_key_count)++;
}

bool typio_wl_startup_guard_track_press(bool *suppressed_keys,
                                        size_t suppressed_keys_len,
                                        size_t *suppressed_key_count,
                                        bool suppress_stale_keys,
                                        uint64_t started_at_ms,
                                        uint64_t now_ms,
                                        uint32_t keycode,
                                        uint32_t keysym) {
    if (!suppressed_keys || !suppressed_key_count) {
        return false;
    }

    if (startup_guard_valid_keycode(suppressed_keys_len, keycode) &&
        suppressed_keys[keycode]) {
        return true;
    }

    if (typio_wl_startup_guard_should_ignore_enter(started_at_ms, now_ms, keysym) ||
        suppress_stale_keys) {
        startup_guard_mark_suppressed(suppressed_keys,
                                      suppressed_keys_len,
                                      suppressed_key_count,
                                      keycode);
        return true;
    }

    return false;
}

bool typio_wl_startup_guard_track_release(bool *suppressed_keys,
                                          size_t suppressed_keys_len,
                                          size_t *suppressed_key_count,
                                          bool *suppress_stale_keys,
                                          uint32_t keycode) {
    bool was_suppressed = false;

    if (!suppressed_keys || !suppressed_key_count || !suppress_stale_keys) {
        return false;
    }

    if (startup_guard_valid_keycode(suppressed_keys_len, keycode) &&
        suppressed_keys[keycode]) {
        suppressed_keys[keycode] = false;
        if (*suppressed_key_count > 0) {
            (*suppressed_key_count)--;
        }
        was_suppressed = true;
    }

    if (*suppress_stale_keys && *suppressed_key_count == 0) {
        *suppress_stale_keys = false;
        return true;
    }

    return was_suppressed;
}
