/**
 * @file keyboard_xkb.c
 * @brief XKB state management for Wayland keyboard grabs
 *
 * Isolated from wl_keyboard.c so that XKB details do not leak into the
 * key-routing and grab-handling layers.
 */

#include "wl_frontend_internal.h"
#include "keyboard_xkb.h"
#include "vk_bridge.h"
#include "utils/log.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void xkb_load_keymap(TypioWlKeyboard *keyboard,
                            uint32_t format,
                            int fd,
                            uint32_t size) {
    char *map_str = nullptr;
    struct xkb_keymap *keymap = nullptr;
    struct xkb_state *state = nullptr;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        typio_log(TYPIO_LOG_WARNING, "Unsupported keymap format: %u", format);
        goto cleanup;
    }

    map_str = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_str == MAP_FAILED) {
        typio_log(TYPIO_LOG_ERROR, "Failed to mmap keymap: %s", strerror(errno));
        goto cleanup;
    }

    keymap = xkb_keymap_new_from_string(keyboard->xkb_context,
                                        map_str,
                                        XKB_KEYMAP_FORMAT_TEXT_V1,
                                        XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        typio_log(TYPIO_LOG_ERROR, "Failed to compile XKB keymap");
        goto cleanup;
    }

    state = xkb_state_new(keymap);
    if (!state) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create XKB state");
        goto cleanup;
    }

    xkb_keymap_unref(keyboard->xkb_keymap);
    xkb_state_unref(keyboard->xkb_state);
    keyboard->xkb_keymap = keymap;
    keyboard->xkb_state = state;

    keyboard->mod_shift = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    keyboard->mod_ctrl  = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
    keyboard->mod_alt   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
    keyboard->mod_super = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO);
    keyboard->mod_caps  = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
    keyboard->mod_num   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_NUM);

    typio_log(TYPIO_LOG_DEBUG, "XKB keymap updated");

cleanup:
    if (map_str && map_str != MAP_FAILED) munmap(map_str, size);
    if (fd >= 0) close(fd);
}

static void xkb_apply_modifiers(TypioWlKeyboard *keyboard,
                                uint32_t mods_depressed,
                                uint32_t mods_latched,
                                uint32_t mods_locked,
                                uint32_t group) {
    if (!keyboard->xkb_state) return;

    xkb_state_update_mask(keyboard->xkb_state,
                          mods_depressed,
                          mods_latched,
                          mods_locked,
                          0, 0, group);
    keyboard->mods_depressed = mods_depressed;
    keyboard->mods_latched   = mods_latched;
    keyboard->mods_locked    = mods_locked;
    keyboard->mods_group     = group;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

void typio_wl_keyboard_handle_keymap(TypioWlKeyboard *keyboard,
                                      uint32_t format,
                                      int32_t fd,
                                      uint32_t size) {
    if (!keyboard) {
        if (fd >= 0) close(fd);
        return;
    }
    xkb_load_keymap(keyboard, format, fd, size);
}

void typio_wl_keyboard_handle_modifiers(TypioWlKeyboard *keyboard,
                                         uint32_t mods_depressed,
                                         uint32_t mods_latched,
                                         uint32_t mods_locked,
                                         uint32_t group) {
    if (!keyboard) return;
    xkb_apply_modifiers(keyboard, mods_depressed, mods_latched, mods_locked, group);
}

uint32_t typio_wl_keyboard_keysym(TypioWlKeyboard *keyboard, uint32_t key) {
    if (!keyboard || !keyboard->xkb_state) return 0;
    return xkb_state_key_get_one_sym(keyboard->xkb_state, key + 8);
}

uint32_t typio_wl_keyboard_unicode(TypioWlKeyboard *keyboard, uint32_t key) {
    if (!keyboard || !keyboard->xkb_state) return 0;
    char buf[8] = {0};
    int n = xkb_state_key_get_utf8(keyboard->xkb_state, key + 8, buf, sizeof(buf));
    if (n == 1) return (uint32_t)buf[0];
    if (n == 2) return ((uint32_t)(unsigned char)buf[0] << 8) | (uint8_t)buf[1];
    return 0;
}
