/**
 * @file shortcut_chord.h
 * @brief Pure policy for modifier-only shortcut chords
 */

#ifndef TYPIO_WL_SHORTCUT_CHORD_H
#define TYPIO_WL_SHORTCUT_CHORD_H

#include "typio/types.h"

#include <stdbool.h>
#include <stdint.h>

bool typio_wl_shortcut_chord_is_switch_modifier(uint32_t keysym);
bool typio_wl_shortcut_chord_should_switch_engine(uint32_t keysym,
                                                  uint32_t modifiers,
                                                  bool saw_non_modifier,
                                                  bool already_triggered);
bool typio_wl_shortcut_chord_should_reset(uint32_t physical_modifiers);

#endif /* TYPIO_WL_SHORTCUT_CHORD_H */
