/**
 * @file repeat_guard.h
 * @brief Helpers for deciding when keyboard repeat should be cancelled
 */

#ifndef TYPIO_WL_REPEAT_GUARD_H
#define TYPIO_WL_REPEAT_GUARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool typio_wl_repeat_should_cancel_on_modifier_transition(
    uint32_t previous_modifiers,
    uint32_t current_modifiers);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_REPEAT_GUARD_H */
