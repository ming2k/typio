/**
 * @file startup_guard.h
 * @brief Epoch-based startup filtering for freshly activated keyboard grabs
 *
 * When a new keyboard grab is established, the compositor may re-send press
 * events for keys that are already physically held.  These "inherited" presses
 * are not genuine new user input and must be suppressed.
 *
 * The guard uses the Wayland dispatch epoch (a counter incremented after each
 * wl_display_dispatch) to identify these re-sent presses deterministically:
 * any press that arrives within the first TYPIO_WL_STARTUP_GUARD_EPOCHS
 * dispatch cycles after the grab was created is treated as stale.
 *
 * This replaces the previous time-based 50ms window, which was racy when
 * JavaScript processing or compositor roundtrips introduced latency between
 * the physical keypress and the IME activation.
 */

#ifndef TYPIO_WL_STARTUP_GUARD_H
#define TYPIO_WL_STARTUP_GUARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Number of Wayland dispatch epochs after grab creation during which key
 * presses are treated as potential compositor re-sends of held keys.
 *
 * The grab request is a Wayland client→server message.  The compositor
 * responds with keymap, modifiers, and any held-key press events.  These
 * responses arrive in the NEXT dispatch cycle (epoch + 1).  We guard for
 * two cycles to handle compositors that batch the response across cycles.
 */
#define TYPIO_WL_STARTUP_GUARD_EPOCHS 2ULL

typedef enum {
    TYPIO_WL_STARTUP_SUPPRESS_NONE = 0,
    TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY,
} TypioWlStartupSuppressReason;

bool typio_wl_startup_guard_is_in_guard_window(uint64_t created_at_epoch,
                                                uint64_t current_epoch);
TypioWlStartupSuppressReason typio_wl_startup_guard_classify_press(
    uint64_t created_at_epoch,
    uint64_t current_epoch,
    bool suppress_stale_keys);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_STARTUP_GUARD_H */
