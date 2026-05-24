/**
 * @file resume_signal.h
 * @brief System-resume detector for the Wayland frontend
 *
 * The composition lifecycle's most user-visible failure mode is a stuck
 * modifier or runaway repeat after the laptop wakes up: while suspended
 * the kernel cannot deliver a key-up, and on resume the compositor may
 * not always emit a clean deactivate/activate round-trip. The lifecycle
 * therefore needs its own first-class "the system just resumed"
 * notification independent of @c zwp_input_method_v2 events.
 *
 * This module exposes two complementary detectors that converge on the
 * same single callback:
 *
 *  1. @b logind @c PrepareForSleep — system-bus signal emitted by
 *     systemd-logind around any suspend/hibernate transition. Reliable
 *     when present, but absent on non-systemd distros and on minimal
 *     containers. Only built when libdbus is available
 *     (@c HAVE_STATUS_BUS, since that already pulls in the dependency).
 *
 *  2. @b boottime/monotonic-gap heuristic — @c CLOCK_BOOTTIME advances
 *     during suspend while @c CLOCK_MONOTONIC does not. The event loop
 *     calls @c typio_wl_resume_signal_tick once per iteration; any gap
 *     greater than @c TYPIO_WL_RESUME_GAP_THRESHOLD_MS is treated as
 *     "the kernel just woke us up." Always built, so it serves as a
 *     fallback when logind is missing or its signal is lost.
 *
 * Both sources de-duplicate on a short cooldown window so a coincident
 * logind notification and detected gap fire the resume handler only
 * once.
 */

#ifndef TYPIO_WL_RESUME_SIGNAL_H
#define TYPIO_WL_RESUME_SIGNAL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioWlResumeSignal TypioWlResumeSignal;

/**
 * Resume callback. @c reason is a stable string literal identifying
 * which detector fired (e.g. "logind", "boottime_gap"); useful for trace
 * output but not interpreted by the lifecycle layer.
 *
 * The callback runs synchronously on the event-loop thread.
 */
typedef void (*TypioWlResumeCallback)(void *user_data,
                                      const char *reason,
                                      uint64_t sleep_ms);

TypioWlResumeSignal *typio_wl_resume_signal_create(TypioWlResumeCallback cb,
                                                   void *user_data);

void typio_wl_resume_signal_destroy(TypioWlResumeSignal *rs);

/**
 * Pollable fd for the logind connection, or -1 when DBus is unavailable
 * (the gap detector still works without an fd; the event loop just
 * skips this aux handler entry).
 */
int typio_wl_resume_signal_get_fd(TypioWlResumeSignal *rs);

/**
 * Drain pending logind messages. Safe to call when fd is -1; returns
 * immediately.
 */
int typio_wl_resume_signal_dispatch(TypioWlResumeSignal *rs);

/**
 * Per-iteration tick. Compares CLOCK_BOOTTIME against CLOCK_MONOTONIC
 * to detect a suspend gap. Cheap (two clock_gettime calls and a
 * subtract); safe to call every event-loop turn.
 */
void typio_wl_resume_signal_tick(TypioWlResumeSignal *rs);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_RESUME_SIGNAL_H */
