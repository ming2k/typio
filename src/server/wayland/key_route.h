/**
 * @file key_route.h
 * @brief Key press/release routing for Wayland keyboard events
 */

#ifndef TYPIO_WL_KEY_ROUTE_H
#define TYPIO_WL_KEY_ROUTE_H

#include "shortcut_config.h"

#include <stdint.h>

struct TypioWlKeyboard;
struct TypioWlSession;

typedef enum {
    TYPIO_WL_KEY_ROUTE_NONE = 0,
    TYPIO_WL_KEY_ROUTE_TYPIO_RESERVED,
    TYPIO_WL_KEY_ROUTE_APPLICATION_SHORTCUT,
} TypioWlKeyRouteClass;

typedef enum {
    TYPIO_WL_RESERVED_ACTION_NONE = 0,
    TYPIO_WL_RESERVED_ACTION_EMERGENCY_EXIT,
    TYPIO_WL_RESERVED_ACTION_VOICE_PTT,
} TypioWlReservedAction;

#ifdef __cplusplus
extern "C" {
#endif

const char *typio_wl_key_route_class_name(TypioWlKeyRouteClass route_class);
const char *typio_wl_reserved_action_name(TypioWlReservedAction action);
bool typio_wl_key_route_binding_matches_press(const TypioShortcutBinding *binding,
                                              uint32_t keysym,
                                              uint32_t modifiers);
TypioWlKeyRouteClass typio_wl_key_route_classify_shortcut(
    const TypioShortcutConfig *shortcuts,
    uint32_t keysym,
    uint32_t modifiers);
TypioWlReservedAction typio_wl_key_route_reserved_action(
    const TypioShortcutConfig *shortcuts,
    uint32_t keysym,
    uint32_t modifiers);
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
