/**
 * @file candidate_guard.h
 * @brief Helpers for reserving navigation keys while candidate UI is active
 */

#ifndef TYPIO_WL_CANDIDATE_GUARD_H
#define TYPIO_WL_CANDIDATE_GUARD_H

#include "typio/input_context.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool typio_wl_candidate_guard_is_navigation_keysym(uint32_t keysym);
bool typio_wl_candidate_guard_should_consume(TypioInputContext *ctx,
                                             uint32_t keysym);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_CANDIDATE_GUARD_H */
