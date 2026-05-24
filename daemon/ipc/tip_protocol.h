/**
 * @file tip_protocol.h
 * @brief Typio IPC Protocol (TIP) constants — UDS transport
 *
 * Replaces dbus_protocol.h for the UDS control surface.
 * Property and method names are kept identical to the D-Bus
 * surface so the D-Bus adapter can delegate without translation.
 */

#ifndef TYPIO_TIP_PROTOCOL_H
#define TYPIO_TIP_PROTOCOL_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return the canonical UDS socket path.
 *
 * Prefers $XDG_RUNTIME_DIR/typio/daemon.sock.
 * Falls back to ~/.local/share/typio/daemon.sock.
 * Caller must free() the returned string.
 */
char *typio_ipc_socket_path(void);

/* ---------- JSON-RPC 2.0 methods ---------- */
#define TYPIO_IPC_METHOD_GETALL          "GetAll"
#define TYPIO_IPC_METHOD_GET             "Get"
#define TYPIO_IPC_METHOD_ACTIVATE_ENGINE "ActivateEngine"
#define TYPIO_IPC_METHOD_NEXT_ENGINE     "NextEngine"
#define TYPIO_IPC_METHOD_SET_RIME_SCHEMA "SetRimeSchema"
#define TYPIO_IPC_METHOD_DEPLOY_RIME     "DeployRimeConfig"
#define TYPIO_IPC_METHOD_SET_CONFIG_TEXT "SetConfigText"
#define TYPIO_IPC_METHOD_RELOAD_CONFIG   "ReloadConfig"
#define TYPIO_IPC_METHOD_STOP            "Stop"

/* ---------- Property names ---------- */
#define TYPIO_IPC_PROP_VERSION                    "Version"
#define TYPIO_IPC_PROP_ACTIVE_KEYBOARD_ENGINE     "ActiveKeyboardEngine"
#define TYPIO_IPC_PROP_ACTIVE_ENGINE              "ActiveEngine"
#define TYPIO_IPC_PROP_AVAILABLE_KEYBOARD_ENGINES "AvailableKeyboardEngines"
#define TYPIO_IPC_PROP_AVAILABLE_ENGINES          "AvailableEngines"
#define TYPIO_IPC_PROP_ORDERED_KEYBOARD_ENGINES   "OrderedKeyboardEngines"
#define TYPIO_IPC_PROP_ORDERED_ENGINES            "OrderedEngines"
#define TYPIO_IPC_PROP_ENGINE_DISPLAY_NAMES       "EngineDisplayNames"
#define TYPIO_IPC_PROP_ENGINE_ORDER               "EngineOrder"
#define TYPIO_IPC_PROP_AVAILABLE_VOICE_ENGINES    "AvailableVoiceEngines"
#define TYPIO_IPC_PROP_ACTIVE_VOICE_ENGINE        "ActiveVoiceEngine"
#define TYPIO_IPC_PROP_ACTIVE_ENGINE_STATE        "ActiveEngineState"
#define TYPIO_IPC_PROP_ACTIVE_ENGINE_MODE         "ActiveEngineMode"
#define TYPIO_IPC_PROP_RUNTIME_STATE              "RuntimeState"
#define TYPIO_IPC_PROP_RIME_SCHEMA                "RimeSchema"
#define TYPIO_IPC_PROP_CONFIG_TEXT                "ConfigText"

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_TIP_PROTOCOL_H */
