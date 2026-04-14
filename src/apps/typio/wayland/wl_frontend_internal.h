/**
 * @file wl_frontend_internal.h
 * @brief Internal structures for Wayland frontend
 */

#ifndef TYPIO_WL_FRONTEND_INTERNAL_H
#define TYPIO_WL_FRONTEND_INTERNAL_H

#include "wl_frontend.h"
#include "key_arbiter.h"
#include "key_tracking.h"
#include "shortcut_config.h"
#include "lifecycle.h"
#include "keyboard_repeat.h"
#include "identity.h"
#include "startup_guard.h"
#include "text_ui_backend.h"
#include "vk_bridge.h"
#include "typio/types.h"
#include "typio_build_config.h"

#include <wayland-client.h>
#include <pthread.h>
#include <xkbcommon/xkbcommon.h>
#include <stdio.h>
#include <stdint.h>

#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

#ifdef HAVE_STATUS_BUS
#include "status/status.h"
#endif
#ifdef HAVE_SYSTRAY
#include "tray/tray.h"
#endif
#ifdef HAVE_VOICE
#include "voice/voice_service.h"
#endif

#ifdef __cplusplus
#include <atomic>
#define TYPIO_ATOMIC(t) std::atomic<t>
#else
#include <stdatomic.h>
#define TYPIO_ATOMIC(t) _Atomic t
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct TypioWlSession TypioWlSession;
typedef struct TypioWlKeyboard TypioWlKeyboard;
typedef struct TypioWlCandidatePopup TypioWlCandidatePopup;
typedef struct TypioWlOutput TypioWlOutput;

struct TypioWlTextUiBackend {
    TypioWlFrontend *frontend;
    TypioWlCandidatePopup *candidate_popup;
};

typedef enum TypioWlLoopStage {
    TYPIO_WL_LOOP_STAGE_IDLE = 0,
    TYPIO_WL_LOOP_STAGE_POPUP_UPDATE,
    TYPIO_WL_LOOP_STAGE_PREPARE_READ,
    TYPIO_WL_LOOP_STAGE_FLUSH,
    TYPIO_WL_LOOP_STAGE_POLL,
    TYPIO_WL_LOOP_STAGE_READ_EVENTS,
    TYPIO_WL_LOOP_STAGE_DISPATCH_PENDING,
    TYPIO_WL_LOOP_STAGE_AUX_IO,
    TYPIO_WL_LOOP_STAGE_REPEAT,
    TYPIO_WL_LOOP_STAGE_CONFIG_RELOAD,
} TypioWlLoopStage;

#define TYPIO_WL_MAX_TRACKED_KEYS 512

/**
 * @brief Per-key tracking state.
 *
 * This state machine records what happened to a key after routing decided
 * whether to consume or forward it. It is not itself the routing decision
 * model; action/reason routing decisions live in key_route.*.
 *
 * Each key can be in exactly one tracking state. Transitions:
 *
 *   IDLE ──press──▶ TRACK_FORWARDED          (forwarded to app)
 *   IDLE ──press──▶ TRACK_BASIC_PASSTHROUGH  (basic printable text bypasses engine)
 *   IDLE ──press──▶ TRACK_APP_SHORTCUT       (application shortcut bypasses engine)
 *   IDLE ──press──▶ TRACK_SUPPRESSED_STARTUP (held key from previous grab)
 *   TRACK_APP_SHORTCUT ─physical release──▶ IDLE
 *   TRACK_FORWARDED ─force release──▶ TRACK_RELEASED_PENDING
 *   TRACK_FORWARDED ─physical release──▶ IDLE
 *   TRACK_BASIC_PASSTHROUGH ─force release──▶ TRACK_RELEASED_PENDING
 *   TRACK_BASIC_PASSTHROUGH ─physical release──▶ IDLE
 *   TRACK_RELEASED_PENDING ─physical release──▶ IDLE  (consumed)
 *   TRACK_SUPPRESSED_STARTUP ─physical release──▶ IDLE
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
    uint32_t mods_depressed;
    uint32_t mods_latched;
    uint32_t mods_locked;
    uint32_t mods_group;
    bool saw_blocking_modifier;

    /* Key repeat */
    int32_t repeat_rate;    /* Keys per second */
    int32_t repeat_delay;   /* Delay before repeat starts (ms) */
    int repeat_timer_fd;    /* timerfd for key repeat */
    uint32_t repeat_key;    /* Key currently repeating */
    uint32_t repeat_time;   /* Timestamp of the original press */
    bool repeating;         /* Whether a key is currently repeating */

    /* Startup guard: suppress stale keys held from previous grab.
     * Keys that arrive as presses within the first two Wayland dispatch
     * epochs after the grab is created are treated as compositor re-sends
     * of already-held keys, not genuine new user input. */
    bool suppress_stale_keys;
    size_t startup_suppressed_count;
    uint64_t created_at_epoch;

    /* Key event arbiter for system shortcut detection */
    TypioKeyArbiter arbiter;

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

    /* Last preedit sent to app (for change detection) */
    char *last_preedit_text;
    int last_preedit_cursor;

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
    TypioWlOutput *outputs;

    /* Input method protocol objects */
    struct zwp_input_method_manager_v2 *im_manager;
    struct zwp_input_method_v2 *input_method;

    /* Virtual keyboard for forwarding unhandled keys */
    struct zwp_virtual_keyboard_manager_v1 *vk_manager;
    struct zwp_virtual_keyboard_v1 *virtual_keyboard;
    TypioWlVirtualKeyboardState virtual_keyboard_state;
    bool virtual_keyboard_has_keymap;
    uint32_t virtual_keyboard_keymap_generation;
    uint64_t virtual_keyboard_drop_count;
    uint64_t virtual_keyboard_keymap_cancel_count;
    uint64_t virtual_keyboard_keymap_cancel_window_start_ms;
    uint64_t virtual_keyboard_state_since_ms;
    uint64_t virtual_keyboard_last_keymap_ms;
    uint64_t virtual_keyboard_last_forward_ms;
    uint64_t virtual_keyboard_keymap_deadline_ms;
    uint64_t guard_reject_press_streak;
    uint64_t guard_reject_press_window_start_ms;
    TYPIO_ATOMIC(uint64_t) watchdog_heartbeat_ms;
    TYPIO_ATOMIC(uint64_t) watchdog_stage_since_ms;
    TYPIO_ATOMIC(bool) watchdog_stop;
    TYPIO_ATOMIC(bool) watchdog_armed;
    TYPIO_ATOMIC(int) watchdog_loop_stage;
    pthread_t watchdog_thread;
    bool watchdog_thread_started;
    TypioKeyTrackState key_states[TYPIO_WL_MAX_TRACKED_KEYS];
    uint32_t key_generations[TYPIO_WL_MAX_TRACKED_KEYS];
    uint32_t active_key_generation;
    uint64_t trace_sequence;
    bool active_generation_owned_keys;
    bool active_generation_vk_dirty;
    bool carried_vk_modifiers;

    /* Session and keyboard state */
    TypioWlSession *session;
    TypioWlKeyboard *keyboard;
    TypioWlTextUiBackend *text_ui_backend;
    TypioWlIdentityProvider *identity_provider;
    TypioWlIdentity current_identity;

#ifdef HAVE_STATUS_BUS
    TypioStatusBus *status_bus;
#endif
#ifdef HAVE_SYSTRAY
    /* System tray */
    TypioTray *tray;
#endif
#ifdef HAVE_VOICE
    TypioVoiceService *voice;
#endif

    /* Protocol serial: must increment on every done, even without a session */
    uint32_t im_serial;

    /* Wayland dispatch epoch — incremented after each wl_display_dispatch.
     * Used by the startup guard to deterministically identify stale key
     * presses re-sent by the compositor when a new keyboard grab is
     * established, replacing the previous time-based 50ms window. */
    uint64_t dispatch_epoch;

    /* Event loop state */
    volatile bool running;
    TypioWlLifecyclePhase lifecycle_phase;
    bool pending_reactivation;
    bool popup_update_pending;
    int config_watch_fd;
    int config_dir_watch;
    int config_engines_watch;

    /* Configurable shortcut bindings */
    TypioShortcutConfig shortcuts;

    /* Error message buffer */
    char error_msg[256];
};

struct TypioWlOutput {
    TypioWlFrontend *frontend;
    uint32_t name;
    struct wl_output *output;
    int scale;
    TypioWlOutput *next;
};

void typio_wl_frontend_emit_runtime_state_changed(TypioWlFrontend *frontend);
void typio_wl_frontend_watchdog_heartbeat(TypioWlFrontend *frontend);
void typio_wl_frontend_watchdog_set_stage(TypioWlFrontend *frontend,
                                          TypioWlLoopStage stage);
void typio_wl_frontend_watchdog_start(TypioWlFrontend *frontend);
void typio_wl_frontend_watchdog_stop(TypioWlFrontend *frontend);
void typio_wl_frontend_log_shortcuts(TypioWlFrontend *frontend,
                                     const char *prefix);
void typio_wl_frontend_handle_config_watch(TypioWlFrontend *frontend);

/* Input method functions (wl_input_method.c) */
void typio_wl_input_method_setup(TypioWlFrontend *frontend);

/* Session functions */
TypioWlSession *typio_wl_session_create(TypioWlFrontend *frontend);
void typio_wl_session_destroy(TypioWlSession *session);
void typio_wl_session_reset(TypioWlSession *session);
void typio_wl_session_apply_pending(TypioWlSession *session);
void typio_wl_session_flush_ui_update(TypioWlSession *session);

/* Keyboard functions (wl_keyboard.c) */
TypioWlKeyboard *typio_wl_keyboard_create(TypioWlFrontend *frontend);
void typio_wl_keyboard_destroy(TypioWlKeyboard *keyboard);
void typio_wl_keyboard_release_grab(TypioWlKeyboard *keyboard);
void typio_wl_keyboard_cancel_repeat(TypioWlKeyboard *keyboard);
int typio_wl_keyboard_get_repeat_fd(TypioWlKeyboard *keyboard);
void typio_wl_keyboard_dispatch_repeat(TypioWlKeyboard *keyboard);
void typio_wl_keyboard_process_key_press(TypioWlKeyboard *keyboard,
                                         TypioWlSession *session,
                                         uint32_t key, uint32_t keysym,
                                         uint32_t modifiers, uint32_t unicode,
                                         uint32_t time);
void typio_wl_keyboard_process_key_release(TypioWlKeyboard *keyboard,
                                           TypioWlSession *session,
                                           uint32_t key, uint32_t keysym,
                                           uint32_t modifiers, uint32_t unicode,
                                           uint32_t time);

/* Candidate popup implementation (candidate_popup.c) */
typedef struct TypioWlCandidatePopup TypioWlCandidatePopup;
TypioWlCandidatePopup *typio_wl_candidate_popup_create(TypioWlFrontend *frontend);
void typio_wl_candidate_popup_destroy(TypioWlCandidatePopup *candidate_popup);
bool typio_wl_candidate_popup_update(TypioWlTextUiBackend *backend, TypioInputContext *ctx);
void typio_wl_candidate_popup_hide(TypioWlTextUiBackend *backend);
bool typio_wl_candidate_popup_is_available(TypioWlTextUiBackend *backend);
void typio_wl_candidate_popup_invalidate_config(TypioWlTextUiBackend *backend);
void typio_wl_candidate_popup_handle_output_change(TypioWlTextUiBackend *backend,
                                                   struct wl_output *output);

/* Commit helpers */
void typio_wl_commit_string(TypioWlFrontend *frontend, const char *text);
void typio_wl_set_preedit(TypioWlFrontend *frontend, const char *text,
                          int cursor_begin, int cursor_end);
void typio_wl_commit(TypioWlFrontend *frontend);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_FRONTEND_INTERNAL_H */
