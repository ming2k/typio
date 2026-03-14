/**
 * @file startup_guard.c
 * @brief Time-based startup filtering for freshly activated keyboard grabs
 */

#include "startup_guard.h"
bool typio_wl_startup_guard_should_ignore_stale_press(uint64_t started_at_ms,
                                                      uint64_t now_ms) {
    if (now_ms < started_at_ms)
        return false;

    return (now_ms - started_at_ms) <= TYPIO_WL_STARTUP_STALE_KEY_GUARD_MS;
}

bool typio_wl_startup_guard_should_cleanup_stale_release(uint64_t started_at_ms,
                                                         uint64_t now_ms) {
    return typio_wl_startup_guard_should_ignore_stale_press(started_at_ms,
                                                            now_ms);
}

TypioWlStartupSuppressReason typio_wl_startup_guard_classify_press(
    uint64_t started_at_ms,
    uint64_t now_ms,
    bool suppress_stale_keys) {

    if (suppress_stale_keys &&
        typio_wl_startup_guard_should_ignore_stale_press(started_at_ms, now_ms)) {
        return TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY;
    }

    return TYPIO_WL_STARTUP_SUPPRESS_NONE;
}
