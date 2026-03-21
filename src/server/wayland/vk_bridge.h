/**
 * @file vk_bridge.h
 * @brief Virtual keyboard forwarding helpers
 */

#ifndef TYPIO_WL_VK_BRIDGE_H
#define TYPIO_WL_VK_BRIDGE_H

#include "key_tracking.h"

#include <stdint.h>

struct TypioWlKeyboard;
struct TypioWlFrontend;

typedef enum {
    TYPIO_WL_VK_STATE_ABSENT = 0,
    TYPIO_WL_VK_STATE_NEEDS_KEYMAP,
    TYPIO_WL_VK_STATE_READY,
    TYPIO_WL_VK_STATE_BROKEN,
} TypioWlVirtualKeyboardState;

#ifdef __cplusplus
extern "C" {
#endif

const char *typio_wl_vk_state_name(TypioWlVirtualKeyboardState state);
void typio_wl_vk_set_state(struct TypioWlFrontend *frontend,
                           TypioWlVirtualKeyboardState state,
                           const char *reason);
bool typio_wl_vk_is_ready(struct TypioWlFrontend *frontend,
                          const char *operation);
void typio_wl_vk_forward_key(struct TypioWlKeyboard *keyboard,
                             uint32_t time,
                             uint32_t key,
                             uint32_t state,
                             uint32_t unicode);
void typio_wl_vk_forward_modifiers(struct TypioWlKeyboard *keyboard,
                                   uint32_t mods_depressed,
                                   uint32_t mods_latched,
                                   uint32_t mods_locked,
                                   uint32_t group);
void typio_wl_vk_forward_modifier_state(struct TypioWlFrontend *frontend,
                                        uint32_t mods_depressed,
                                        uint32_t mods_latched,
                                        uint32_t mods_locked,
                                        uint32_t group);
void typio_wl_vk_release_forwarded_keys(struct TypioWlFrontend *frontend,
                                        const char *(*key_state_name)(TypioKeyTrackState state));
void typio_wl_vk_reset_modifiers(struct TypioWlFrontend *frontend);
void typio_wl_vk_forward_keymap(struct TypioWlFrontend *frontend,
                                uint32_t format,
                                int32_t fd,
                                uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_VK_BRIDGE_H */
