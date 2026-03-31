/**
 * @file shortcut_chord.h
 * @brief Pure policy for modifier-only shortcut chords
 */

#ifndef TYPIO_WL_SHORTCUT_CHORD_H
#define TYPIO_WL_SHORTCUT_CHORD_H

#include "shortcut_config.h"
#include "typio/types.h"

#include <stdint.h>

/**
 * Is this keysym one of the modifiers required by the switch-engine binding?
 */
bool typio_wl_shortcut_chord_is_switch_modifier(
    const TypioShortcutBinding *binding, uint32_t keysym);

/**
 * Should the engine be switched right now?
 */
bool typio_wl_shortcut_chord_should_switch_engine(
    const TypioShortcutBinding *binding,
    uint32_t keysym, uint32_t modifiers,
    bool saw_non_modifier, bool already_triggered);

/**
 * Have all chord modifiers been released?
 */
bool typio_wl_shortcut_chord_should_reset(
    const TypioShortcutBinding *binding, uint32_t physical_modifiers);

#endif /* TYPIO_WL_SHORTCUT_CHORD_H */
