/**
 * @file vk_bridge.c
 * @brief Virtual keyboard forwarding helpers
 */


#include "vk_bridge.h"
#include "key_debug.h"
#include "wl_frontend_internal.h"
#include "wl_trace.h"
#include "utils/log.h"

#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

static uint64_t vk_bridge_monotonic_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;

    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

void typio_wl_vk_forward_key(struct TypioWlKeyboard *keyboard,
                             uint32_t time,
                             uint32_t key,
                             uint32_t state,
                             uint32_t unicode) {
    const char *reason = "forward";
    TypioWlFrontend *frontend;
    char unicode_desc[64];

    if (!keyboard)
        return;

    frontend = keyboard->frontend;
    if (!frontend || !frontend->virtual_keyboard ||
        !frontend->virtual_keyboard_has_keymap)
        return;

    frontend->active_generation_vk_dirty = true;
    if (key < TYPIO_WL_MAX_TRACKED_KEYS &&
        frontend->key_states[key] == TYPIO_KEY_RELEASED_PENDING) {
        reason = "synthetic_release";
    }
    typio_wl_key_debug_format(unicode, unicode_desc, sizeof(unicode_desc));

    typio_wl_trace(frontend,
                   "vk_key",
                   "state=%s keycode=%u time=%u %s reason=%s",
                   state == WL_KEYBOARD_KEY_STATE_PRESSED ? "press" : "release",
                   key, time, unicode_desc, reason);
    zwp_virtual_keyboard_v1_key(frontend->virtual_keyboard, time, key, state);
}

void typio_wl_vk_forward_modifiers(struct TypioWlKeyboard *keyboard,
                                   uint32_t mods_depressed,
                                   uint32_t mods_latched,
                                   uint32_t mods_locked,
                                   uint32_t group) {
    TypioWlFrontend *frontend;

    if (!keyboard)
        return;

    frontend = keyboard->frontend;
    if (!frontend || !frontend->virtual_keyboard ||
        !frontend->virtual_keyboard_has_keymap)
        return;

    frontend->active_generation_vk_dirty = true;
    typio_wl_trace(frontend,
                   "vk_modifiers",
                   "depressed=0x%x latched=0x%x locked=0x%x group=%u",
                   mods_depressed, mods_latched, mods_locked, group);
    zwp_virtual_keyboard_v1_modifiers(frontend->virtual_keyboard,
                                      mods_depressed, mods_latched,
                                      mods_locked, group);
}

void typio_wl_vk_forward_modifier_state(TypioWlFrontend *frontend,
                                        uint32_t mods_depressed,
                                        uint32_t mods_latched,
                                        uint32_t mods_locked,
                                        uint32_t group) {
    if (!frontend || !frontend->virtual_keyboard ||
        !frontend->virtual_keyboard_has_keymap)
        return;

    frontend->active_generation_vk_dirty = true;
    typio_wl_trace(frontend,
                   "vk_modifiers",
                   "depressed=0x%x latched=0x%x locked=0x%x group=%u",
                   mods_depressed, mods_latched, mods_locked, group);
    zwp_virtual_keyboard_v1_modifiers(frontend->virtual_keyboard,
                                      mods_depressed, mods_latched,
                                      mods_locked, group);
}

void typio_wl_vk_release_forwarded_keys(TypioWlFrontend *frontend,
                                        const char *(*key_state_name)(TypioKeyTrackState state)) {
    size_t released;
    uint32_t time;
    bool use_generic_name;

    if (!frontend || !frontend->virtual_keyboard ||
        !frontend->virtual_keyboard_has_keymap)
        return;

    time = (uint32_t)vk_bridge_monotonic_ms();
    use_generic_name = key_state_name == nullptr;

    for (size_t key = 0; key < TYPIO_WL_MAX_TRACKED_KEYS; key++) {
        if (frontend->key_states[key] != TYPIO_KEY_FORWARDED &&
            frontend->key_states[key] != TYPIO_KEY_APP_SHORTCUT)
            continue;

        typio_wl_trace(frontend,
                       "vk_key",
                       "state=release keycode=%zu time=%u unicode=none char=- reason=hard_reset route=%s",
                       key, time,
                       use_generic_name ? "tracked"
                                        : key_state_name(frontend->key_states[key]));
        zwp_virtual_keyboard_v1_key(frontend->virtual_keyboard,
                                    time, (uint32_t)key,
                                    WL_KEYBOARD_KEY_STATE_RELEASED);
    }

    released = typio_wl_key_tracking_mark_released_pending(frontend->key_states,
                                                           TYPIO_WL_MAX_TRACKED_KEYS);
    if (released > 0) {
        typio_log(TYPIO_LOG_DEBUG,
                  "Force-released %zu forwarded keys at lifecycle boundary",
                  released);
    }
}

void typio_wl_vk_reset_modifiers(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->virtual_keyboard ||
        !frontend->virtual_keyboard_has_keymap)
        return;

    typio_wl_trace(frontend,
                   "vk_reset_modifiers",
                   "depressed=0x0 latched=0x0 locked=0x0 group=0");
    zwp_virtual_keyboard_v1_modifiers(frontend->virtual_keyboard, 0, 0, 0, 0);
    frontend->carried_vk_modifiers = false;

    if (frontend->keyboard) {
        frontend->keyboard->physical_modifiers = 0;
        frontend->keyboard->mods_depressed = 0;
        frontend->keyboard->mods_latched = 0;
        frontend->keyboard->mods_locked = 0;
        frontend->keyboard->mods_group = 0;
        if (frontend->keyboard->xkb_state) {
            xkb_state_update_mask(frontend->keyboard->xkb_state,
                                  0, 0, 0, 0, 0, 0);
        }
    }
}

void typio_wl_vk_forward_keymap(TypioWlFrontend *frontend,
                                uint32_t format,
                                int32_t fd,
                                uint32_t size) {
    int vk_fd;

    if (!frontend || !frontend->virtual_keyboard)
        return;

    vk_fd = dup(fd);
    if (vk_fd < 0)
        return;

    zwp_virtual_keyboard_v1_keymap(frontend->virtual_keyboard, format, vk_fd, size);
    frontend->virtual_keyboard_has_keymap = true;
    typio_wl_trace(frontend,
                   "keymap",
                   "stage=forwarded_to_vk format=%u size=%u",
                   format, size);
}
