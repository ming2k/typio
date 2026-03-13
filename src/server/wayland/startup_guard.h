/**
 * @file startup_guard.h
 * @brief Time-based startup filtering for freshly activated keyboard grabs
 *
 * Two small time windows are used:
 * - a very short stale-key window to absorb held keys replayed into a new grab
 * - a longer Enter window to prevent accidental immediate submission
 */

#ifndef TYPIO_WL_STARTUP_GUARD_H
#define TYPIO_WL_STARTUP_GUARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TYPIO_WL_STARTUP_STALE_KEY_GUARD_MS 50ULL
#define TYPIO_WL_STARTUP_ENTER_GUARD_MS 1000ULL

typedef enum {
    TYPIO_WL_STARTUP_SUPPRESS_NONE = 0,
    TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY,
    TYPIO_WL_STARTUP_SUPPRESS_ENTER,
} TypioWlStartupSuppressReason;

bool typio_wl_startup_guard_should_ignore_stale_press(uint64_t started_at_ms,
                                                      uint64_t now_ms);
bool typio_wl_startup_guard_should_cleanup_stale_release(uint64_t started_at_ms,
                                                         uint64_t now_ms);
bool typio_wl_startup_guard_should_ignore_enter(uint64_t started_at_ms,
                                                uint64_t now_ms,
                                                uint32_t keysym);
TypioWlStartupSuppressReason typio_wl_startup_guard_classify_press(
    uint64_t started_at_ms,
    uint64_t now_ms,
    uint32_t keysym,
    bool has_composition,
    bool suppress_stale_keys);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_STARTUP_GUARD_H */
