/**
 * @file keyboard_xkb.h
 * @brief XKB state management for Wayland keyboard grabs
 */

#ifndef TYPIO_KEYBOARD_XKB_H
#define TYPIO_KEYBOARD_XKB_H

#include "wl_frontend_internal.h"

void typio_wl_keyboard_handle_keymap(TypioWlKeyboard *keyboard,
                                      uint32_t format,
                                      int32_t fd,
                                      uint32_t size);

void typio_wl_keyboard_handle_modifiers(TypioWlKeyboard *keyboard,
                                         uint32_t mods_depressed,
                                         uint32_t mods_latched,
                                         uint32_t mods_locked,
                                         uint32_t group);

/* typio_wl_xkb_effective_modifiers is defined inline in xkb_modifiers.h */
uint32_t typio_wl_keyboard_keysym(TypioWlKeyboard *keyboard, uint32_t key);
uint32_t typio_wl_keyboard_unicode(TypioWlKeyboard *keyboard, uint32_t key);

#endif /* TYPIO_KEYBOARD_XKB_H */
