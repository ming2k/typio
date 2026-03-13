/**
 * @file key_route.h
 * @brief Key press/release routing for Wayland keyboard events
 */

#ifndef TYPIO_WL_KEY_ROUTE_H
#define TYPIO_WL_KEY_ROUTE_H

#include <stdint.h>

struct TypioWlKeyboard;
struct TypioWlSession;

#ifdef __cplusplus
extern "C" {
#endif

void typio_wl_key_route_process_press(struct TypioWlKeyboard *keyboard,
                                      struct TypioWlSession *session,
                                      uint32_t key,
                                      uint32_t keysym,
                                      uint32_t modifiers,
                                      uint32_t unicode,
                                      uint32_t time);
void typio_wl_key_route_process_release(struct TypioWlKeyboard *keyboard,
                                        struct TypioWlSession *session,
                                        uint32_t key,
                                        uint32_t keysym,
                                        uint32_t modifiers,
                                        uint32_t unicode,
                                        uint32_t time);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_KEY_ROUTE_H */
