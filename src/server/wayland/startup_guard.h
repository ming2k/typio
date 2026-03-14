/**
 * @file startup_guard.h
 * @brief Time-based startup filtering for freshly activated keyboard grabs
 *
 * Startup suppression is only for the very early "stale key may still be held"
 * window on a fresh grab.
 */

#ifndef TYPIO_WL_STARTUP_GUARD_H
#define TYPIO_WL_STARTUP_GUARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TYPIO_WL_STARTUP_STALE_KEY_GUARD_MS 50ULL
#define TYPIO_WL_STARTUP_ENTER_GUARD_MS TYPIO_WL_STARTUP_STALE_KEY_GUARD_MS

typedef enum {
    TYPIO_WL_STARTUP_SUPPRESS_NONE = 0,
    TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY,
} TypioWlStartupSuppressReason;

bool typio_wl_startup_guard_should_ignore_stale_press(uint64_t started_at_ms,
                                                      uint64_t now_ms);
bool typio_wl_startup_guard_should_cleanup_stale_release(uint64_t started_at_ms,
                                                         uint64_t now_ms);
TypioWlStartupSuppressReason typio_wl_startup_guard_classify_press(
    uint64_t started_at_ms,
    uint64_t now_ms,
    bool suppress_stale_keys);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_STARTUP_GUARD_H */
