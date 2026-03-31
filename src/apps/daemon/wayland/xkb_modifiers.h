/**
 * @file xkb_modifiers.h
 * @brief Inline helpers for mapping XKB effective modifiers to Typio flags
 */

#ifndef TYPIO_WL_XKB_MODIFIERS_H
#define TYPIO_WL_XKB_MODIFIERS_H

#include "wl_frontend_internal.h"

static inline uint32_t typio_wl_xkb_effective_modifiers(TypioWlKeyboard *keyboard) {
    struct xkb_state *state;
    uint32_t mods = TYPIO_MOD_NONE;

    if (!keyboard || !keyboard->xkb_state) {
        return mods;
    }

    state = keyboard->xkb_state;

    if (xkb_state_mod_index_is_active(state, keyboard->mod_shift, XKB_STATE_MODS_EFFECTIVE)) {
        mods |= TYPIO_MOD_SHIFT;
    }
    if (xkb_state_mod_index_is_active(state, keyboard->mod_ctrl, XKB_STATE_MODS_EFFECTIVE)) {
        mods |= TYPIO_MOD_CTRL;
    }
    if (xkb_state_mod_index_is_active(state, keyboard->mod_alt, XKB_STATE_MODS_EFFECTIVE)) {
        mods |= TYPIO_MOD_ALT;
    }
    if (xkb_state_mod_index_is_active(state, keyboard->mod_super, XKB_STATE_MODS_EFFECTIVE)) {
        mods |= TYPIO_MOD_SUPER;
    }
    if (xkb_state_mod_index_is_active(state, keyboard->mod_caps, XKB_STATE_MODS_EFFECTIVE)) {
        mods |= TYPIO_MOD_CAPSLOCK;
    }
    if (xkb_state_mod_index_is_active(state, keyboard->mod_num, XKB_STATE_MODS_EFFECTIVE)) {
        mods |= TYPIO_MOD_NUMLOCK;
    }

    return mods;
}

#endif /* TYPIO_WL_XKB_MODIFIERS_H */
