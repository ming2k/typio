/**
 * @file keyboard_repeat.h
 * @brief Keyboard repeat helpers for Wayland keyboard grabs
 */

#ifndef TYPIO_WL_KEYBOARD_REPEAT_H
#define TYPIO_WL_KEYBOARD_REPEAT_H

struct TypioWlKeyboard;

#ifdef __cplusplus
extern "C" {
#endif

void typio_wl_keyboard_repeat_maybe_start(struct TypioWlKeyboard *keyboard,
                                          unsigned int key,
                                          unsigned int time,
                                          unsigned int modifiers);
void typio_wl_keyboard_repeat_stop(struct TypioWlKeyboard *keyboard);
void typio_wl_keyboard_cancel_repeat(struct TypioWlKeyboard *keyboard);
int typio_wl_keyboard_get_repeat_fd(struct TypioWlKeyboard *keyboard);
void typio_wl_keyboard_dispatch_repeat(struct TypioWlKeyboard *keyboard);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_KEYBOARD_REPEAT_H */
