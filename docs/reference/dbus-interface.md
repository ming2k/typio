# D-Bus Interface Reference

Typio exposes a session-bus service for runtime control and introspection.
Both `typio-client` and `typio-control` are built on this interface.

## Service

| Field     | Value                        |
|-----------|------------------------------|
| Bus       | Session bus                  |
| Service   | `org.typio.InputMethod1`     |
| Path      | `/org/typio/InputMethod1`    |
| Interface | `org.typio.InputMethod1`     |

The service is registered when `typio` starts with `ENABLE_STATUS_BUS=ON`
(the default). The constants live in `src/core/include/typio/dbus_protocol.h`.

## Properties

All properties are read-only. The daemon emits
`org.freedesktop.DBus.Properties.PropertiesChanged` whenever active-engine
state, config, or runtime diagnostics change.

| Property | Type | Description |
|----------|------|-------------|
| `Version` | `s` | Build version string |
| `ActiveKeyboardEngine` | `s` | Currently active keyboard engine name |
| `ActiveEngine` | `s` | Alias for `ActiveKeyboardEngine` |
| `AvailableKeyboardEngines` | `as` | All registered keyboard engines |
| `AvailableEngines` | `as` | All registered engines (keyboard + voice) |
| `OrderedKeyboardEngines` | `as` | Keyboard engines in switch-cycle order |
| `OrderedEngines` | `as` | Alias for `OrderedKeyboardEngines` |
| `EngineDisplayNames` | `a{ss}` | Engine name to display name mapping |
| `EngineOrder` | `as` | Full `engine_order` array from config |
| `AvailableVoiceEngines` | `as` | Registered voice engines |
| `ActiveVoiceEngine` | `s` | Currently active voice engine name |
| `ActiveEngineMode` | `a{sv}` | Active engine mode dict (see below) |
| `ActiveEngineState` | `a{sv}` | Active keyboard engine detail dict (see below) |
| `RuntimeState` | `a{sv}` | Wayland frontend diagnostics dict (see below) |
| `RimeSchema` | `s` | Current Rime input schema identifier |
| `ConfigText` | `s` | Full `typio.toml` contents as text |

### `ActiveEngineMode` keys

| Key | Type | Description |
|-----|------|-------------|
| `mode_class` | `s` | Coarse classification: `"native"` or `"latin"` |
| `mode_id` | `s` | Engine-specific mode identifier (e.g. `"chinese"`, `"hiragana"`, `"direct"`) |
| `display_label` | `s` | Short display label (e.g. `"中"`, `"あ"`, `"A"`) |
| `icon_name` | `s` | Icon name for this mode (e.g. `"typio-rime"`, `"typio-mozc-katakana"`) |

This property is empty (`a{sv}{}`) when no engine is active or the engine
does not report mode information. It updates alongside `PropertiesChanged`
whenever the engine's mode changes.

### `ActiveEngineState` keys

| Key | Type | Description |
|-----|------|-------------|
| `name` | `s` | Engine name |
| `display_name` | `s` | Human-readable name |
| `icon` | `s` | Icon name |
| `language` | `s` | Language code |
| `engine_type` | `s` | `"keyboard"` or `"voice"` |
| `capabilities` | `u` | Capability flags |
| `active` | `b` | Whether the engine is active |
| `mode_class` | `s` | Current mode class (`"native"` or `"latin"`) |
| `mode_id` | `s` | Current mode identifier |
| `mode_display_label` | `s` | Current mode display label |
| `mode_icon` | `s` | Current mode icon name |
| `config_path` | `s` | Path to engine config file |
| `config.*` | varies | Engine-specific config entries |

### `RuntimeState` keys

| Key | Type | Description |
|-----|------|-------------|
| `frontend_backend` | `s` | e.g. `"wayland"` |
| `lifecycle_phase` | `s` | e.g. `"active"`, `"inactive"` |
| `virtual_keyboard_state` | `s` | e.g. `"ready"`, `"broken"` |
| `keyboard_grab_active` | `b` | Whether keyboard is grabbed |
| `virtual_keyboard_has_keymap` | `b` | Keymap loaded |
| `watchdog_armed` | `b` | Watchdog status |
| `active_key_generation` | `u` | Current keyboard-grab generation |
| `virtual_keyboard_keymap_generation` | `u` | Generation that most recently delivered a keymap |
| `virtual_keyboard_drop_count` | `u` | Dropped key count |
| `virtual_keyboard_state_age_ms` | `u` | State age in ms |
| `virtual_keyboard_keymap_age_ms` | `u` | Keymap age in ms |
| `virtual_keyboard_forward_age_ms` | `u` | Forward age in ms |
| `virtual_keyboard_keymap_deadline_remaining_ms` | `i` | Deadline remaining in ms |

## Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `ActivateEngine` | `s -> ()` | Switch to named keyboard engine; saves to config |
| `NextEngine` | `() -> ()` | Cycle to the next keyboard engine (same as Ctrl+Shift) |
| `SetRimeSchema` | `s -> ()` | Set the active Rime schema and reload |
| `SetConfigText` | `s -> ()` | Replace entire config from text |
| `ReloadConfig` | `() -> ()` | Reload config from disk |
| `Stop` | `() -> ()` | Shut down the daemon |

All methods return a D-Bus error reply on failure.

## Signals

The daemon emits `PropertiesChanged` on the standard
`org.freedesktop.DBus.Properties` interface after every method that
modifies state. Clients can subscribe with:

```
type='signal',interface='org.freedesktop.DBus.Properties',
path='/org/typio/InputMethod1'
```

## Quick Examples

### busctl

```bash
# Query active engine
busctl --user get-property org.typio.InputMethod1 \
  /org/typio/InputMethod1 org.typio.InputMethod1 ActiveKeyboardEngine

# Switch engine
busctl --user call org.typio.InputMethod1 \
  /org/typio/InputMethod1 org.typio.InputMethod1 ActivateEngine s rime

# Cycle to next engine
busctl --user call org.typio.InputMethod1 \
  /org/typio/InputMethod1 org.typio.InputMethod1 NextEngine
```

### typio-client

```bash
typio-client engine             # print active keyboard engine
typio-client engine list        # list engines (* marks active)
typio-client engine next        # cycle to next engine
typio-client engine rime        # switch to rime
typio-client schema             # print current Rime schema
typio-client schema luna_pinyin # set Rime schema
typio-client config reload      # reload config from disk
typio-client config get         # print current config text
typio-client status             # show server status summary
typio-client stop               # stop the daemon
typio-client version            # show server version
```

## Implementation Notes

- The server-side handler lives in `src/apps/daemon/status/status.c`.
- Protocol constants are in `src/core/include/typio/dbus_protocol.h`.
- `typio-client` source is in `src/apps/cli/main.c` — a pure D-Bus client
  with no dependency on `typio-core`.
- `typio-control` (GTK4) uses the same D-Bus interface via GDBusProxy.
- The status bus integration test is in `tests/test_status_bus.c`.
