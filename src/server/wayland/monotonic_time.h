/**
 * @file monotonic_time.h
 * @brief Inline helpers for Wayland-side monotonic timestamps
 */

#ifndef TYPIO_WL_MONOTONIC_TIME_H
#define TYPIO_WL_MONOTONIC_TIME_H

#include <stdint.h>
#include <time.h>

static inline uint64_t typio_wl_monotonic_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

#endif /* TYPIO_WL_MONOTONIC_TIME_H */
