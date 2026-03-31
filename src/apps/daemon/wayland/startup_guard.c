/**
 * @file startup_guard.c
 * @brief Epoch-based startup filtering for freshly activated keyboard grabs
 */

#include "startup_guard.h"

bool typio_wl_startup_guard_is_in_guard_window(uint64_t created_at_epoch,
                                                uint64_t current_epoch) {
    if (current_epoch < created_at_epoch)
        return false;

    return (current_epoch - created_at_epoch) <= TYPIO_WL_STARTUP_GUARD_EPOCHS;
}

TypioWlStartupSuppressReason typio_wl_startup_guard_classify_press(
    uint64_t created_at_epoch,
    uint64_t current_epoch,
    bool suppress_stale_keys) {

    if (suppress_stale_keys &&
        typio_wl_startup_guard_is_in_guard_window(created_at_epoch,
                                                   current_epoch)) {
        return TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY;
    }

    return TYPIO_WL_STARTUP_SUPPRESS_NONE;
}
