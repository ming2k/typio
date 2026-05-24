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
    TYPIO_WL_RESERVED_ACTION_NONE = 0,
    TYPIO_WL_RESERVED_ACTION_EMERGENCY_EXIT,
    TYPIO_WL_RESERVED_ACTION_VOICE_PTT,
} TypioWlReservedAction;

typedef enum {
    TYPIO_WL_KEY_ACTION_CONSUME = 0,
    TYPIO_WL_KEY_ACTION_FORWARD,
} TypioWlKeyAction;

typedef enum {
    TYPIO_WL_KEY_REASON_NONE = 0,
    TYPIO_WL_KEY_REASON_TYPIO_RESERVED,
    TYPIO_WL_KEY_REASON_APPLICATION_SHORTCUT,
    TYPIO_WL_KEY_REASON_BASIC_PASSTHROUGH,
    TYPIO_WL_KEY_REASON_ENGINE_HANDLED,
    TYPIO_WL_KEY_REASON_ENGINE_UNHANDLED,
    TYPIO_WL_KEY_REASON_MODIFIER_PASSTHROUGH,
    TYPIO_WL_KEY_REASON_CANDIDATE_NAVIGATION,
    TYPIO_WL_KEY_REASON_STARTUP_SUPPRESSED,
    TYPIO_WL_KEY_REASON_RELEASED_PENDING,
    TYPIO_WL_KEY_REASON_LATCHED_APP_SHORTCUT,
    TYPIO_WL_KEY_REASON_LATCHED_FORWARDED,
    TYPIO_WL_KEY_REASON_STARTUP_STALE_CLEANUP,
    TYPIO_WL_KEY_REASON_FORWARDED_RELEASE,
    TYPIO_WL_KEY_REASON_ORPHAN_RELEASE_CLEANUP,
    TYPIO_WL_KEY_REASON_ORPHAN_RELEASE_CONSUMED,
    TYPIO_WL_KEY_REASON_VOICE_PTT,
    TYPIO_WL_KEY_REASON_VOICE_PTT_UNAVAILABLE,
} TypioWlKeyReason;

typedef struct TypioWlKeyDecision {
    TypioWlKeyAction action;
    TypioWlKeyReason reason;
} TypioWlKeyDecision;

#ifdef __cplusplus
extern "C" {
#endif

const char *typio_wl_key_action_name(TypioWlKeyAction action);
const char *typio_wl_key_reason_name(TypioWlKeyReason reason);
const char *typio_wl_reserved_action_name(TypioWlReservedAction action);
bool typio_wl_key_route_binding_matches_press(const TypioShortcutBinding *binding,
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
