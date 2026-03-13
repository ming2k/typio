/**
 * @file boundary_bridge.c
 * @brief Pure boundary-bridge policy for activation/deactivation handoff
 */

#include "boundary_bridge.h"
#include "typio/event.h"
#include "typio/types.h"

bool typio_wl_boundary_bridge_should_cleanup_orphan_release(
    uint32_t keysym,
    uint32_t modifiers,
    bool saw_blocking_modifier) {
    if (keysym == TYPIO_KEY_Return || keysym == TYPIO_KEY_KP_Enter)
        return true;

    return saw_blocking_modifier ||
           (modifiers & (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) != 0;
}

bool typio_wl_boundary_bridge_should_reset_carried_modifiers(
    TypioWlLifecyclePhase phase,
    bool carried_vk_modifiers) {
    return carried_vk_modifiers && phase != TYPIO_WL_PHASE_DEACTIVATING;
}

bool typio_wl_boundary_bridge_should_carry_modifiers(
    TypioWlLifecyclePhase phase,
    bool own_current_generation,
    uint32_t mods_depressed,
    uint32_t mods_latched,
    uint32_t mods_locked) {
    if (!own_current_generation || phase != TYPIO_WL_PHASE_DEACTIVATING)
        return false;

    return mods_depressed != 0 || mods_latched != 0 || mods_locked != 0;
}
