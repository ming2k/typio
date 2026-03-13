/**
 * @file modifier_policy.c
 * @brief Effective modifier policy for Wayland keyboard events
 */

#include "modifier_policy.h"

#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon-keysyms.h>

static uint32_t modifier_bit_for_keysym(uint32_t keysym) {
    switch (keysym) {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
        return TYPIO_MOD_SHIFT;
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
        return TYPIO_MOD_CTRL;
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
        return TYPIO_MOD_ALT;
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
        return TYPIO_MOD_SUPER;
    default:
        return TYPIO_MOD_NONE;
    }
}

static uint32_t modifiers_for_current_key(uint32_t modifiers,
                                          uint32_t keysym,
                                          uint32_t state) {
    uint32_t bit = modifier_bit_for_keysym(keysym);

    if (bit == TYPIO_MOD_NONE)
        return modifiers;

    return state == WL_KEYBOARD_KEY_STATE_PRESSED
        ? (modifiers | bit)
        : (modifiers & ~bit);
}

uint32_t typio_wl_modifier_policy_effective_modifiers(uint32_t physical_modifiers,
                                                      uint32_t xkb_modifiers,
                                                      bool active_generation_owned_keys,
                                                      uint32_t keysym,
                                                      uint32_t state) {
    uint32_t blocking = TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER;
    uint32_t locks = xkb_modifiers & (TYPIO_MOD_CAPSLOCK | TYPIO_MOD_NUMLOCK);
    uint32_t modifiers = physical_modifiers | locks;

    if (!active_generation_owned_keys)
        modifiers |= xkb_modifiers & blocking;

    return modifiers_for_current_key(modifiers, keysym, state);
}

uint32_t typio_wl_modifier_policy_sync_physical_modifiers(uint32_t physical_modifiers,
                                                          uint32_t xkb_modifiers) {
    uint32_t blocking = TYPIO_MOD_SHIFT | TYPIO_MOD_CTRL |
                        TYPIO_MOD_ALT | TYPIO_MOD_SUPER;

    return (physical_modifiers & ~blocking) | (xkb_modifiers & blocking);
}
