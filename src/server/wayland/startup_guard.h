/**
 * @file startup_guard.h
 * @brief Startup key filtering for freshly activated Wayland keyboard grabs
 */

#ifndef TYPIO_WL_STARTUP_GUARD_H
#define TYPIO_WL_STARTUP_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TYPIO_WL_STARTUP_ENTER_GUARD_MS 1000ULL

bool typio_wl_startup_guard_should_ignore_enter(uint64_t started_at_ms,
                                                uint64_t now_ms,
                                                uint32_t keysym);
bool typio_wl_startup_guard_track_press(bool *suppressed_keys,
                                        size_t suppressed_keys_len,
                                        size_t *suppressed_key_count,
                                        bool suppress_stale_keys,
                                        uint64_t started_at_ms,
                                        uint64_t now_ms,
                                        uint32_t keycode,
                                        uint32_t keysym);
bool typio_wl_startup_guard_track_release(bool *suppressed_keys,
                                          size_t suppressed_keys_len,
                                          size_t *suppressed_key_count,
                                          bool *suppress_stale_keys,
                                          uint32_t keycode);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_STARTUP_GUARD_H */
