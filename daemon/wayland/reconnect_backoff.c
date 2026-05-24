/**
 * @file reconnect_backoff.c
 * @brief Pure capped-exponential backoff (see reconnect_backoff.h)
 */

#include "reconnect_backoff.h"

uint32_t typio_wl_reconnect_delay_ms(uint32_t attempt) {
    uint32_t delay;

    /* Cap the shift before computing 2^attempt so the multiply cannot
     * overflow; once base<<shift would exceed the max we just clamp. */
    if (attempt >= 16u)
        return TYPIO_WL_RECONNECT_MAX_DELAY_MS;

    delay = TYPIO_WL_RECONNECT_BASE_DELAY_MS << attempt;
    if (delay > TYPIO_WL_RECONNECT_MAX_DELAY_MS ||
        delay < TYPIO_WL_RECONNECT_BASE_DELAY_MS /* wrapped */)
        return TYPIO_WL_RECONNECT_MAX_DELAY_MS;
    return delay;
}

bool typio_wl_reconnect_should_retry(uint32_t attempt) {
    return attempt < TYPIO_WL_RECONNECT_MAX_ATTEMPTS;
}
