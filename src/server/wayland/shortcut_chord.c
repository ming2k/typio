/**
 * @file shortcut_chord.c
 * @brief Pure policy for modifier-only shortcut chords
 */

#include "shortcut_chord.h"

#include "typio/event.h"

bool typio_wl_shortcut_chord_is_switch_modifier(uint32_t keysym) {
    return keysym == TYPIO_KEY_Control_L ||
           keysym == TYPIO_KEY_Control_R ||
           keysym == TYPIO_KEY_Shift_L ||
           keysym == TYPIO_KEY_Shift_R;
}

bool typio_wl_shortcut_chord_should_switch_engine(uint32_t keysym,
                                                  uint32_t modifiers,
                                                  bool saw_non_modifier,
                                                  bool already_triggered) {
    uint32_t required = TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT;
    uint32_t unsupported = TYPIO_MOD_ALT | TYPIO_MOD_SUPER;

    if (already_triggered || saw_non_modifier)
        return false;

    if (!typio_wl_shortcut_chord_is_switch_modifier(keysym))
        return false;

    if ((modifiers & required) != required)
        return false;

    return (modifiers & unsupported) == 0;
}

bool typio_wl_shortcut_chord_should_reset(uint32_t physical_modifiers) {
    return (physical_modifiers & (TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT |
                                  TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) == 0;
}
