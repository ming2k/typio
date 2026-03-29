/**
 * @file dbus_protocol.h
 * @brief Shared D-Bus service, path, and interface constants
 */

#ifndef TYPIO_DBUS_PROTOCOL_H
#define TYPIO_DBUS_PROTOCOL_H

#define TYPIO_STATUS_DBUS_SERVICE "org.typio.InputMethod1"
#define TYPIO_STATUS_DBUS_PATH "/org/typio/InputMethod1"
#define TYPIO_STATUS_DBUS_INTERFACE "org.typio.InputMethod1"

#define TYPIO_STATUS_PROP_VERSION "Version"
#define TYPIO_STATUS_PROP_ACTIVE_KEYBOARD_ENGINE "ActiveKeyboardEngine"
#define TYPIO_STATUS_PROP_ACTIVE_ENGINE "ActiveEngine"
#define TYPIO_STATUS_PROP_AVAILABLE_KEYBOARD_ENGINES "AvailableKeyboardEngines"
#define TYPIO_STATUS_PROP_AVAILABLE_ENGINES "AvailableEngines"
#define TYPIO_STATUS_PROP_ORDERED_KEYBOARD_ENGINES "OrderedKeyboardEngines"
#define TYPIO_STATUS_PROP_ORDERED_ENGINES "OrderedEngines"
#define TYPIO_STATUS_PROP_ENGINE_DISPLAY_NAMES "EngineDisplayNames"
#define TYPIO_STATUS_PROP_ENGINE_ORDER "EngineOrder"
#define TYPIO_STATUS_PROP_AVAILABLE_VOICE_ENGINES "AvailableVoiceEngines"
#define TYPIO_STATUS_PROP_ACTIVE_VOICE_ENGINE "ActiveVoiceEngine"
#define TYPIO_STATUS_PROP_ACTIVE_ENGINE_STATE "ActiveEngineState"
#define TYPIO_STATUS_PROP_ACTIVE_ENGINE_MODE "ActiveEngineMode"
#define TYPIO_STATUS_PROP_RUNTIME_STATE "RuntimeState"
#define TYPIO_STATUS_PROP_RIME_SCHEMA "RimeSchema"
#define TYPIO_STATUS_PROP_CONFIG_TEXT "ConfigText"

#define TYPIO_STATUS_METHOD_ACTIVATE_ENGINE "ActivateEngine"
#define TYPIO_STATUS_METHOD_NEXT_ENGINE "NextEngine"
#define TYPIO_STATUS_METHOD_SET_RIME_SCHEMA "SetRimeSchema"
#define TYPIO_STATUS_METHOD_SET_CONFIG_TEXT "SetConfigText"
#define TYPIO_STATUS_METHOD_RELOAD_CONFIG "ReloadConfig"
#define TYPIO_STATUS_METHOD_STOP "Stop"

#endif /* TYPIO_DBUS_PROTOCOL_H */
