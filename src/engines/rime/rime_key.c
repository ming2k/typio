/**
 * @file rime_key.c
 * @brief Key-event translation helpers (modifiers, special keys)
 */

#include "rime_internal.h"

bool typio_rime_is_shift_keysym(uint32_t keysym) {
    return keysym == TYPIO_KEY_Shift_L || keysym == TYPIO_KEY_Shift_R;
}

uint32_t typio_rime_modifiers_to_mask(uint32_t modifiers) {
    uint32_t mask = 0;

    if (modifiers & TYPIO_MOD_SHIFT) {
        mask |= TYPIO_RIME_SHIFT_MASK;
    }
    if (modifiers & TYPIO_MOD_CTRL) {
        mask |= TYPIO_RIME_CONTROL_MASK;
    }
    if (modifiers & TYPIO_MOD_ALT) {
        mask |= TYPIO_RIME_MOD1_MASK;
    }
    if (modifiers & TYPIO_MOD_SUPER) {
        mask |= TYPIO_RIME_MOD4_MASK;
    }
    if (modifiers & TYPIO_MOD_CAPSLOCK) {
        mask |= TYPIO_RIME_LOCK_MASK;
    }
    if (modifiers & TYPIO_MOD_NUMLOCK) {
        mask |= TYPIO_RIME_MOD2_MASK;
    }

    return mask;
}
