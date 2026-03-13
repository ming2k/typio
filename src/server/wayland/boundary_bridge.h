/**
 * @file boundary_bridge.h
 * @brief Pure boundary-bridge policy for activation/deactivation handoff
 */

#ifndef TYPIO_WL_BOUNDARY_BRIDGE_H
#define TYPIO_WL_BOUNDARY_BRIDGE_H

#include "lifecycle.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool typio_wl_boundary_bridge_should_cleanup_orphan_release(
    uint32_t modifiers,
    bool saw_blocking_modifier);
bool typio_wl_boundary_bridge_should_reset_carried_modifiers(
    TypioWlLifecyclePhase phase,
    bool carried_vk_modifiers);
bool typio_wl_boundary_bridge_should_carry_modifiers(
    TypioWlLifecyclePhase phase,
    bool own_current_generation,
    uint32_t mods_depressed,
    uint32_t mods_latched,
    uint32_t mods_locked);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_BOUNDARY_BRIDGE_H */
