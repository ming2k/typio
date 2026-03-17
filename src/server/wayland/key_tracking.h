/**
 * @file key_tracking.h
 * @brief Helpers for managing per-key forwarding state across lifecycle boundaries
 */

#ifndef TYPIO_WL_KEY_TRACKING_H
#define TYPIO_WL_KEY_TRACKING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TYPIO_KEY_IDLE = 0,
    TYPIO_KEY_FORWARDED,
    TYPIO_KEY_APP_SHORTCUT,
    TYPIO_KEY_RELEASED_PENDING,
    TYPIO_KEY_SUPPRESSED_STARTUP,
    TYPIO_KEY_VOICE_PTT,
    TYPIO_KEY_VOICE_PTT_UNAVAIL,
} TypioKeyTrackState;

void typio_wl_key_tracking_reset(TypioKeyTrackState *states, size_t count);
void typio_wl_key_tracking_reset_generations(uint32_t *generations,
                                             size_t count);
size_t typio_wl_key_tracking_mark_released_pending(TypioKeyTrackState *states,
                                                   size_t count);
const char *typio_wl_key_tracking_state_name(TypioKeyTrackState state);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_KEY_TRACKING_H */
