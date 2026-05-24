/**
 * @file reconnect_backoff.h
 * @brief Pure capped-exponential backoff schedule for Wayland reconnect
 *
 * Separated so the schedule can be unit-tested without sleeping or a live
 * display. The reconnect loop (wl_frontend.c) consults this for each
 * attempt's delay and the give-up cutoff.
 */

#ifndef TYPIO_WL_RECONNECT_BACKOFF_H
#define TYPIO_WL_RECONNECT_BACKOFF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TYPIO_WL_RECONNECT_BASE_DELAY_MS 250u
#define TYPIO_WL_RECONNECT_MAX_DELAY_MS  8000u
#define TYPIO_WL_RECONNECT_MAX_ATTEMPTS  12u

/**
 * Delay before reconnect attempt @c attempt (0-based): base * 2^attempt,
 * clamped to TYPIO_WL_RECONNECT_MAX_DELAY_MS. The doubling is computed
 * shift-safe so a large @c attempt cannot overflow.
 */
uint32_t typio_wl_reconnect_delay_ms(uint32_t attempt);

/**
 * Whether attempt @c attempt should still be tried. Caps total attempts at
 * TYPIO_WL_RECONNECT_MAX_ATTEMPTS so a compositor that never returns lets
 * the daemon exit and hand off to the service manager instead of spinning
 * forever.
 */
bool typio_wl_reconnect_should_retry(uint32_t attempt);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_RECONNECT_BACKOFF_H */
