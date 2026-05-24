/**
 * @file monotonic_time.h
 * @brief Inline helpers for Wayland-side time sources
 *
 * Two clocks are exposed:
 *
 *  - @c typio_wl_monotonic_ms — @c CLOCK_MONOTONIC; pauses during system
 *    suspend. Use for loop heartbeats, repeat timers, and any other
 *    timeout that must not fire spuriously after the laptop wakes up.
 *
 *  - @c typio_wl_boottime_ms — @c CLOCK_BOOTTIME; keeps ticking through
 *    suspend. Subtracting the two on every event-loop iteration yields
 *    the wall-clock gap that the system slept for, which is the
 *    canonical "the kernel just resumed us" signal in the absence of a
 *    logind @c PrepareForSleep notification.
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

static inline uint64_t typio_wl_boottime_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) {
        return 0;
    }

    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

#endif /* TYPIO_WL_MONOTONIC_TIME_H */
