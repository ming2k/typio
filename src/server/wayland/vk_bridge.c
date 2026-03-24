/**
 * @file vk_bridge.c
 * @brief Virtual keyboard forwarding helpers
 */


#include "vk_bridge.h"
#include "key_debug.h"
#include "monotonic_time.h"
#include "wl_frontend_internal.h"
#include "wl_trace.h"
#include "utils/log.h"

#include <inttypes.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#define TYPIO_WL_VK_NEEDS_KEYMAP_DROP_LIMIT 32
#define TYPIO_WL_VK_KEYMAP_TIMEOUT_MS 1500

const char *typio_wl_vk_state_name(TypioWlVirtualKeyboardState state) {
    switch (state) {
    case TYPIO_WL_VK_STATE_ABSENT:
        return "absent";
    case TYPIO_WL_VK_STATE_NEEDS_KEYMAP:
        return "needs_keymap";
    case TYPIO_WL_VK_STATE_READY:
        return "ready";
    case TYPIO_WL_VK_STATE_BROKEN:
        return "broken";
    default:
        return "unknown";
    }
}

void typio_wl_vk_set_state(TypioWlFrontend *frontend,
                           TypioWlVirtualKeyboardState state,
                           const char *reason) {
    TypioWlVirtualKeyboardState previous;
    TypioLogLevel level;
    uint64_t now_ms;

    if (!frontend)
        return;

    now_ms = typio_wl_monotonic_ms();
    previous = frontend->virtual_keyboard_state;
    frontend->virtual_keyboard_state = state;
    frontend->virtual_keyboard_state_since_ms = now_ms;
    frontend->virtual_keyboard_has_keymap = state == TYPIO_WL_VK_STATE_READY;
    if (state == TYPIO_WL_VK_STATE_READY || state == TYPIO_WL_VK_STATE_BROKEN ||
        state == TYPIO_WL_VK_STATE_ABSENT) {
        frontend->virtual_keyboard_keymap_deadline_ms = 0;
    }

    if (previous == state)
        return;

    typio_wl_trace(frontend,
                   "vk_state",
                   "from=%s to=%s reason=%s dropped=%" PRIu64,
                   typio_wl_vk_state_name(previous),
                   typio_wl_vk_state_name(state),
                   reason ? reason : "no reason",
                   frontend->virtual_keyboard_drop_count);

    level = TYPIO_LOG_INFO;
    if (state == TYPIO_WL_VK_STATE_ABSENT)
        level = TYPIO_LOG_WARNING;
    else if (state == TYPIO_WL_VK_STATE_BROKEN)
        level = TYPIO_LOG_ERROR;

    typio_log(level,
              "Virtual keyboard state changed: %s -> %s (%s, dropped=%" PRIu64 ")",
              typio_wl_vk_state_name(previous),
              typio_wl_vk_state_name(state),
              reason ? reason : "no reason",
              frontend->virtual_keyboard_drop_count);
}

void typio_wl_vk_expect_keymap(TypioWlFrontend *frontend,
                               const char *reason) {
    uint64_t now_ms;

    if (!frontend || !frontend->virtual_keyboard) {
        return;
    }

    now_ms = typio_wl_monotonic_ms();
    frontend->virtual_keyboard_keymap_deadline_ms = now_ms + TYPIO_WL_VK_KEYMAP_TIMEOUT_MS;
    typio_wl_vk_set_state(frontend, TYPIO_WL_VK_STATE_NEEDS_KEYMAP,
                          reason ? reason : "awaiting keymap");
    typio_wl_trace(frontend,
                   "vk_state",
                   "awaiting=keymap timeout_ms=%u reason=%s",
                   TYPIO_WL_VK_KEYMAP_TIMEOUT_MS,
                   reason ? reason : "awaiting keymap");
}

static void typio_wl_vk_trigger_fail_safe(TypioWlFrontend *frontend,
                                          const char *operation,
                                          uint64_t drops) {
    if (!frontend || !frontend->running) {
        return;
    }

    typio_log(TYPIO_LOG_ERROR,
              "Virtual keyboard fail-safe stop: operation=%s state=%s drops=%" PRIu64,
              operation ? operation : "event",
              typio_wl_vk_state_name(frontend->virtual_keyboard_state),
              drops);
    typio_log_dump_recent_to_configured_path("virtual keyboard fail-safe stop");
    if (frontend->keyboard) {
        typio_wl_keyboard_release_grab(frontend->keyboard);
    }
    typio_wl_frontend_stop(frontend);
}

bool typio_wl_vk_is_ready(TypioWlFrontend *frontend,
                          const char *operation) {
    uint64_t drops;

    if (!frontend)
        return false;

    if (frontend->virtual_keyboard &&
        frontend->virtual_keyboard_state == TYPIO_WL_VK_STATE_NEEDS_KEYMAP &&
        frontend->virtual_keyboard_has_keymap) {
        typio_wl_vk_set_state(frontend, TYPIO_WL_VK_STATE_READY,
                              "keymap available");
    }

    if (frontend->virtual_keyboard_state == TYPIO_WL_VK_STATE_READY)
        return true;

    frontend->virtual_keyboard_drop_count++;
    drops = frontend->virtual_keyboard_drop_count;
    if (drops == 1 || drops % 50 == 0) {
        typio_log(TYPIO_LOG_WARNING,
                  "Dropped virtual keyboard %s: state=%s drops=%" PRIu64,
                  operation ? operation : "event",
                  typio_wl_vk_state_name(frontend->virtual_keyboard_state),
                  drops);
    }

    if (frontend->virtual_keyboard_state == TYPIO_WL_VK_STATE_BROKEN ||
        (frontend->virtual_keyboard_state == TYPIO_WL_VK_STATE_NEEDS_KEYMAP &&
         drops >= TYPIO_WL_VK_NEEDS_KEYMAP_DROP_LIMIT)) {
        typio_wl_vk_trigger_fail_safe(frontend, operation, drops);
    }

    return false;
}

void typio_wl_vk_health_check(TypioWlFrontend *frontend) {
    uint64_t now_ms;

    if (!frontend || !frontend->running ||
        frontend->virtual_keyboard_state != TYPIO_WL_VK_STATE_NEEDS_KEYMAP ||
        frontend->virtual_keyboard_keymap_deadline_ms == 0) {
        return;
    }

    now_ms = typio_wl_monotonic_ms();
    if (now_ms < frontend->virtual_keyboard_keymap_deadline_ms) {
        return;
    }

    typio_log(TYPIO_LOG_ERROR,
              "Virtual keyboard keymap timeout: waited=%" PRIu64 "ms since_state=%" PRIu64 "ms last_keymap=%" PRIu64 " last_forward=%" PRIu64,
              now_ms - frontend->virtual_keyboard_state_since_ms,
              frontend->virtual_keyboard_state_since_ms,
              frontend->virtual_keyboard_last_keymap_ms,
              frontend->virtual_keyboard_last_forward_ms);
    typio_wl_vk_trigger_fail_safe(frontend, "keymap timeout",
                                  frontend->virtual_keyboard_drop_count);
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
    if (!frontend || !typio_wl_vk_is_ready(frontend, "key"))
        return;

    frontend->virtual_keyboard_last_forward_ms = typio_wl_monotonic_ms();
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
    if (!frontend || !typio_wl_vk_is_ready(frontend, "modifier update"))
        return;

    frontend->virtual_keyboard_last_forward_ms = typio_wl_monotonic_ms();
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
    if (!frontend || !typio_wl_vk_is_ready(frontend, "modifier carry"))
        return;

    frontend->virtual_keyboard_last_forward_ms = typio_wl_monotonic_ms();
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
        !typio_wl_vk_is_ready(frontend, "hard reset"))
        return;

    time = (uint32_t)typio_wl_monotonic_ms();
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
    if (!frontend || !typio_wl_vk_is_ready(frontend, "modifier reset"))
        return;

    frontend->virtual_keyboard_last_forward_ms = typio_wl_monotonic_ms();
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
    if (vk_fd < 0) {
        typio_wl_vk_set_state(frontend, TYPIO_WL_VK_STATE_BROKEN,
                              "failed to duplicate keymap fd");
        return;
    }

    zwp_virtual_keyboard_v1_keymap(frontend->virtual_keyboard, format, vk_fd, size);
    frontend->virtual_keyboard_last_keymap_ms = typio_wl_monotonic_ms();
    typio_wl_vk_set_state(frontend, TYPIO_WL_VK_STATE_READY,
                          "received compositor keymap");
    typio_wl_trace(frontend,
                   "keymap",
                   "stage=forwarded_to_vk format=%u size=%u",
                   format, size);
}
