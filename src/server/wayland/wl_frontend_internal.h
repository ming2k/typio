/**
 * @file wl_frontend_internal.h
 * @brief Internal structures for Wayland frontend
 */

#ifndef TYPIO_WL_FRONTEND_INTERNAL_H
#define TYPIO_WL_FRONTEND_INTERNAL_H

#include "wl_frontend.h"
#include "key_tracking.h"
#include "lifecycle.h"
#include "keyboard_repeat.h"
#include "startup_guard.h"
#include "vk_bridge.h"
#include "typio/types.h"
#include "typio_build_config.h"

#include <wayland-client.h>
#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include <xkbcommon/xkbcommon.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef HAVE_SYSTRAY
#include "tray.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct TypioWlSession TypioWlSession;
typedef struct TypioWlKeyboard TypioWlKeyboard;
typedef struct TypioWlPopup TypioWlPopup;

#define TYPIO_WL_MAX_TRACKED_KEYS 512

/**
 * @brief Per-key tracking state.
 *
 * Each key can be in exactly one state.  Transitions:
 *
 *   IDLE ──press──▶ FORWARDED          (forwarded to app)
 *   IDLE ──press──▶ APP_SHORTCUT       (application shortcut bypasses engine)
 *   IDLE ──press──▶ SUPPRESSED_STARTUP (held key from previous grab)
 *   IDLE ──press──▶ SUPPRESSED_ENTER   (new Enter press inside startup guard)
 *   APP_SHORTCUT ─physical release──▶ IDLE
 *   FORWARDED ─force release──▶ RELEASED_PENDING
 *   FORWARDED ─physical release──▶ IDLE
 *   RELEASED_PENDING ─physical release──▶ IDLE  (consumed)
 *   SUPPRESSED_STARTUP ─physical release──▶ IDLE
 *   SUPPRESSED_ENTER ─physical release──▶ IDLE
 */

/**
 * @brief Wayland keyboard state (XKB handling)
 */
struct TypioWlKeyboard {
    struct zwp_input_method_keyboard_grab_v2 *grab;

    /* XKB state */
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;

    /* XKB modifier indices (per-keyboard, not global) */
    xkb_mod_index_t mod_shift;
    xkb_mod_index_t mod_ctrl;
    xkb_mod_index_t mod_alt;
    xkb_mod_index_t mod_super;
    xkb_mod_index_t mod_caps;
    xkb_mod_index_t mod_num;
    uint32_t physical_modifiers;

    /* Key repeat */
    int32_t repeat_rate;    /* Keys per second */
    int32_t repeat_delay;   /* Delay before repeat starts (ms) */
    int repeat_timer_fd;    /* timerfd for key repeat */
    uint32_t repeat_key;    /* Key currently repeating */
    uint32_t repeat_time;   /* Timestamp of the original press */
    bool repeating;         /* Whether a key is currently repeating */

    /* Startup guard: suppress stale keys held from previous grab */
    bool suppress_stale_keys;
    size_t startup_suppressed_count;
    uint64_t created_at_ms;

    /* Back reference */
    TypioWlFrontend *frontend;
};

/**
 * @brief Per-activation session state
 */
struct TypioWlSession {
    TypioInputContext *ctx;         /* Typio input context */

    /* Pending state (before done event) */
    struct {
        char *surrounding_text;
        uint32_t cursor;
        uint32_t anchor;
        uint32_t content_hint;
        uint32_t content_purpose;
        uint32_t text_change_cause;
        bool active;
    } pending;

    /* Current state (after done event) */
    struct {
        char *surrounding_text;
        uint32_t cursor;
        uint32_t anchor;
        uint32_t content_hint;
        uint32_t content_purpose;
    } current;

    /* Protocol serial for commit synchronization */
    uint32_t serial;

    /* Back reference */
    TypioWlFrontend *frontend;
};

/**
 * @brief Main Wayland frontend state
 */
struct TypioWlFrontend {
    /* Core Typio instance */
    TypioInstance *instance;

    /* Wayland connection */
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct wl_compositor *compositor;
    struct wl_shm *shm;

    /* Input method protocol objects */
    struct zwp_input_method_manager_v2 *im_manager;
    struct zwp_input_method_v2 *input_method;

    /* Virtual keyboard for forwarding unhandled keys */
    struct zwp_virtual_keyboard_manager_v1 *vk_manager;
    struct zwp_virtual_keyboard_v1 *virtual_keyboard;
    bool virtual_keyboard_has_keymap;
    TypioKeyTrackState key_states[TYPIO_WL_MAX_TRACKED_KEYS];
    uint32_t key_generations[TYPIO_WL_MAX_TRACKED_KEYS];
    uint32_t active_key_generation;
    uint64_t trace_sequence;
    bool active_generation_owned_keys;
    bool active_generation_vk_dirty;

    /* Session and keyboard state */
    TypioWlSession *session;
    TypioWlKeyboard *keyboard;
    TypioWlPopup *popup;

#ifdef HAVE_SYSTRAY
    /* System tray */
    TypioTray *tray;
#endif

    /* Engine change callback */
    void (*engine_change_callback)(void *user_data);
    void *engine_change_user_data;

    /* Event loop state */
    volatile bool running;
    TypioWlLifecyclePhase lifecycle_phase;

    /* Error message buffer */
    char error_msg[256];
};

/* Input method functions (wl_input_method.c) */
void typio_wl_input_method_setup(TypioWlFrontend *frontend);

/* Session functions */
TypioWlSession *typio_wl_session_create(TypioWlFrontend *frontend);
void typio_wl_session_destroy(TypioWlSession *session);
void typio_wl_session_reset(TypioWlSession *session);
void typio_wl_session_apply_pending(TypioWlSession *session);

/* Keyboard functions (wl_keyboard.c) */
TypioWlKeyboard *typio_wl_keyboard_create(TypioWlFrontend *frontend);
void typio_wl_keyboard_destroy(TypioWlKeyboard *keyboard);
void typio_wl_keyboard_release_grab(TypioWlKeyboard *keyboard);
void typio_wl_keyboard_cancel_repeat(TypioWlKeyboard *keyboard);
int typio_wl_keyboard_get_repeat_fd(TypioWlKeyboard *keyboard);
void typio_wl_keyboard_dispatch_repeat(TypioWlKeyboard *keyboard);

/* Popup functions (popup.c) */
TypioWlPopup *typio_wl_popup_create(TypioWlFrontend *frontend);
void typio_wl_popup_destroy(TypioWlPopup *popup);
bool typio_wl_popup_update(TypioWlFrontend *frontend, TypioInputContext *ctx);
void typio_wl_popup_hide(TypioWlFrontend *frontend);
bool typio_wl_popup_is_available(TypioWlFrontend *frontend);

/* Commit helpers */
void typio_wl_commit_string(TypioWlFrontend *frontend, const char *text);
void typio_wl_set_preedit(TypioWlFrontend *frontend, const char *text,
                          int cursor_begin, int cursor_end);
void typio_wl_commit(TypioWlFrontend *frontend);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_FRONTEND_INTERNAL_H */
