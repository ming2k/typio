/**
 * @file shortcut_chord.c
 * @brief Pure policy for modifier-only shortcut chords
 */

#include "shortcut_chord.h"

#include "typio/event.h"

/**
 * Map a modifier bitmask to the set of keysyms that produce those modifiers.
 * Returns true if keysym is a physical key for any of the given modifiers.
 */
static bool keysym_is_modifier_for(uint32_t required_mods, uint32_t keysym) {
    if ((required_mods & TYPIO_MOD_CTRL) &&
        (keysym == TYPIO_KEY_Control_L || keysym == TYPIO_KEY_Control_R))
        return true;
    if ((required_mods & TYPIO_MOD_SHIFT) &&
        (keysym == TYPIO_KEY_Shift_L || keysym == TYPIO_KEY_Shift_R))
        return true;
    if ((required_mods & TYPIO_MOD_ALT) &&
        (keysym == TYPIO_KEY_Alt_L || keysym == TYPIO_KEY_Alt_R))
        return true;
    if ((required_mods & TYPIO_MOD_SUPER) &&
        (keysym == TYPIO_KEY_Super_L || keysym == TYPIO_KEY_Super_R))
        return true;
    return false;
}

/**
 * Modifiers NOT part of the chord — their presence should cancel.
 */
static uint32_t unsupported_modifiers(const TypioShortcutBinding *binding) {
    uint32_t all = TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT |
                   TYPIO_MOD_ALT | TYPIO_MOD_SUPER;
    return all & ~binding->modifiers;
}

bool typio_wl_shortcut_chord_is_switch_modifier(
    const TypioShortcutBinding *binding, uint32_t keysym) {
    return keysym_is_modifier_for(binding->modifiers, keysym);
}

bool typio_wl_shortcut_chord_should_switch_engine(
    const TypioShortcutBinding *binding,
    uint32_t keysym, uint32_t modifiers,
    bool saw_non_modifier, bool already_triggered) {
    if (already_triggered || saw_non_modifier)
        return false;

    if (!keysym_is_modifier_for(binding->modifiers, keysym))
        return false;

    if ((modifiers & binding->modifiers) != binding->modifiers)
        return false;

    return (modifiers & unsupported_modifiers(binding)) == 0;
}

bool typio_wl_shortcut_chord_should_reset(
    const TypioShortcutBinding *binding, uint32_t physical_modifiers) {
    return (physical_modifiers & binding->modifiers) == 0;
}
