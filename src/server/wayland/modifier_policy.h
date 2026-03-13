/**
 * @file modifier_policy.h
 * @brief Effective modifier policy for Wayland keyboard events
 */

#ifndef TYPIO_WL_MODIFIER_POLICY_H
#define TYPIO_WL_MODIFIER_POLICY_H

#include "typio/types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t typio_wl_modifier_policy_effective_modifiers(uint32_t physical_modifiers,
                                                      uint32_t xkb_modifiers,
                                                      bool active_generation_owned_keys,
                                                      uint32_t keysym,
                                                      uint32_t state);
uint32_t typio_wl_modifier_policy_sync_physical_modifiers(uint32_t physical_modifiers,
                                                          uint32_t xkb_modifiers);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_MODIFIER_POLICY_H */
