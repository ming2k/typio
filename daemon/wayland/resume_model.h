/**
 * @file resume_model.h
 * @brief Pure decision rules for the system-resume detector
 *
 * Split out from resume_signal.c so the suspend-gap and fire-cooldown
 * logic can be unit-tested without real clocks or a DBus connection,
 * mirroring the pure-model split used by lifecycle_model.c.
 */

#ifndef TYPIO_WL_RESUME_MODEL_H
#define TYPIO_WL_RESUME_MODEL_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Decide whether a (boottime - monotonic) divergence indicates the kernel
 * suspended the process. CLOCK_BOOTTIME advances during suspend while
 * CLOCK_MONOTONIC does not, so the excess of the boot delta over the
 * monotonic delta is the time spent suspended.
 *
 * @param mono_delta_ms  CLOCK_MONOTONIC elapsed since the last sample.
 * @param boot_delta_ms  CLOCK_BOOTTIME elapsed since the last sample.
 * @param threshold_ms   minimum gap to treat as a real suspend.
 * @param gap_ms_out     optional; receives the computed gap (0 when none).
 * @return true when the gap is at or above @c threshold_ms.
 */
static inline bool typio_wl_resume_gap_exceeded(uint64_t mono_delta_ms,
                                                uint64_t boot_delta_ms,
                                                uint64_t threshold_ms,
                                                uint64_t *gap_ms_out) {
    uint64_t gap;

    if (boot_delta_ms <= mono_delta_ms) {
        if (gap_ms_out)
            *gap_ms_out = 0;
        return false;
    }

    gap = boot_delta_ms - mono_delta_ms;
    if (gap_ms_out)
        *gap_ms_out = gap;
    return gap >= threshold_ms;
}

/**
 * Decide whether a resume fire should be suppressed because another
 * detector (logind vs. gap heuristic) already fired within the cooldown
 * window. Measured in monotonic ms so the window counts active time.
 *
 * @param now_ms        current monotonic timestamp.
 * @param last_fire_ms  monotonic timestamp of the previous fire, or 0 if
 *                      none has fired yet.
 * @param cooldown_ms   suppression window length.
 * @return true when the caller should drop this fire.
 */
static inline bool typio_wl_resume_in_cooldown(uint64_t now_ms,
                                               uint64_t last_fire_ms,
                                               uint64_t cooldown_ms) {
    return last_fire_ms != 0 && (now_ms - last_fire_ms) < cooldown_ms;
}

#endif /* TYPIO_WL_RESUME_MODEL_H */
