/**
 * @file state_controller.h
 * @brief Centralized state controller — single source of truth for runtime surfaces
 *
 * The StateController sits between the Core layer and external runtime surfaces
 * (system tray, D-Bus status bus, etc.). It:
 *
 *   1. Maintains a snapshot of user-visible state (active engine, mode, icon).
 *   2. Provides query APIs so surfaces read state from ONE place instead of
 *      reaching directly into TypioInstance.
 *   3. Broadcasts change notifications to registered listeners so every surface
 *      updates uniformly.
 *
 * All external surfaces (tray, status bus, and any future UI) should base their
 * behaviour on this layer.
 */

#ifndef TYPIO_STATE_CONTROLLER_H
#define TYPIO_STATE_CONTROLLER_H

#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioStateController TypioStateController;

typedef enum {
    TYPIO_STATE_CHANGE_ENGINE,
    TYPIO_STATE_CHANGE_VOICE_ENGINE,
    TYPIO_STATE_CHANGE_MODE,
    TYPIO_STATE_CHANGE_STATUS_ICON,
} TypioStateChangeType;

typedef void (*TypioStateChangeCallback)(void *user_data,
                                         TypioStateChangeType change_type);

typedef struct TypioStateListener {
    void *user_data;
    TypioStateChangeCallback callback;
} TypioStateListener;

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

TypioStateController *typio_state_controller_new(TypioInstance *instance);
void typio_state_controller_free(TypioStateController *ctrl);

/* -------------------------------------------------------------------------- */
/* Listener registration                                                      */
/* -------------------------------------------------------------------------- */

void typio_state_controller_add_listener(TypioStateController *ctrl,
                                         TypioStateListener listener);
void typio_state_controller_remove_listener(TypioStateController *ctrl,
                                            void *user_data);

/* -------------------------------------------------------------------------- */
/* State queries — single source of truth for external surfaces               */
/* -------------------------------------------------------------------------- */

const char *typio_state_controller_get_active_engine_name(
    TypioStateController *ctrl);
const char *typio_state_controller_get_active_engine_display_name(
    TypioStateController *ctrl);
const char *typio_state_controller_get_active_voice_engine_name(
    TypioStateController *ctrl);
const char *typio_state_controller_get_active_voice_engine_display_name(
    TypioStateController *ctrl);
const char *typio_state_controller_get_status_icon(
    TypioStateController *ctrl);
bool typio_state_controller_get_engine_active(
    TypioStateController *ctrl);
const TypioEngineMode *typio_state_controller_get_current_mode(
    TypioStateController *ctrl);

/* -------------------------------------------------------------------------- */
/* Notifications from Core — called by the daemon's Rust→C callbacks          */
/* -------------------------------------------------------------------------- */

void typio_state_controller_notify_engine_changed(
    TypioStateController *ctrl,
    const TypioEngineInfo *info);
void typio_state_controller_notify_voice_engine_changed(
    TypioStateController *ctrl,
    const TypioEngineInfo *info);
void typio_state_controller_notify_mode_changed(
    TypioStateController *ctrl,
    const TypioEngineMode *mode);
void typio_state_controller_notify_status_icon_changed(
    TypioStateController *ctrl,
    const char *icon_name);

/**
 * @brief Re-read all state from Core and broadcast changes.
 *
 * Call once after all listeners have registered (e.g. at startup) so every
 * surface receives an initial sync.
 */
void typio_state_controller_sync(TypioStateController *ctrl);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_STATE_CONTROLLER_H */
