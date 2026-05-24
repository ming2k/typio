# D-Bus Interface Reference

Typio exposes a session-bus service for runtime control and introspection.

> **Note:** The `typio` CLI now uses **UDS** as its primary transport.
> D-Bus remains available for backward compatibility and for
> `typio-control` (GTK4), but all new client development should target
> the UDS IPC protocol (see [IPC Protocol Reference](ipc-protocol.md)).

## Service

| Field     | Value                        |
|-----------|------------------------------|
| Bus       | Session bus                  |
| Service   | `org.typio.InputMethod1`     |
| Path      | `/org/typio/InputMethod1`    |
| Interface | `org.typio.InputMethod1`     |

The D-Bus service is registered when the daemon is built with
`ENABLE_STATUS_BUS=ON` (the default). The adapter is a thin layer
over `TypioStatusService`; all business logic is shared with the UDS
transport. The constants live in `core/include/typio/dbus_protocol.h`.

## Properties

All properties are read-only. The daemon emits `org.freedesktop.DBus.Properties.PropertiesChanged` whenever active-engine state, config, or runtime diagnostics change.

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
| `ActiveEngineMode` | `a{sv}` | Active engine mode dict |
| `ActiveEngineState` | `a{sv}` | Active keyboard engine detail dict |
| `RuntimeState` | `a{sv}` | Wayland frontend diagnostics dict |
| `RimeSchema` | `s` | Current Rime input schema identifier |
| `ConfigText` | `s` | Full `typio.toml` contents as text |

### `ActiveEngineMode` keys

| Key | Type | Description |
|-----|------|-------------|
| `mode_class` | `s` | `"native"` or `"latin"` |
| `mode_id` | `s` | Engine-specific mode identifier |
| `display_label` | `s` | Short display label |
| `icon_name` | `s` | Icon name for this mode |

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
| `mode_class` | `s` | Current mode class |
| `mode_id` | `s` | Current mode identifier |
| `mode_display_label` | `s` | Current mode display label |
| `mode_icon` | `s` | Current mode icon name |
| `config_path` | `s` | Path to engine config file |
| `config.*` | varies | Engine-specific config entries |

### `RuntimeState` keys

> Under [ADR-0011](../adr/0011-composition-and-lifecycle-rewrite.md) the lifecycle fields are reprojected: `lifecycle_phase` becomes the derived `derived_state` plus `grab_state`, and `active_key_generation` becomes `grab_epoch`. The keys below reflect the current surface until that migration lands.

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
| `NextEngine` | `() -> ()` | Cycle to next keyboard engine |
| `SetRimeSchema` | `s -> ()` | Set the active Rime schema and reload |
| `DeployRimeConfig` | `() -> ()` | Rebuild generated Rime config artifacts |
| `SetConfigText` | `s -> ()` | Replace entire config from text |
| `ReloadConfig` | `() -> ()` | Reload config from disk |
| `Stop` | `() -> ()` | Shut down the daemon |

All methods return a D-Bus error reply on failure.

## Signals

The daemon emits `PropertiesChanged` on the standard `org.freedesktop.DBus.Properties` interface after every method that modifies state. Clients can subscribe with:

```
type='signal',interface='org.freedesktop.DBus.Properties',
path='/org/typio/InputMethod1'
```

## Quick examples

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

### typio CLI

```bash
typio engine             # print active keyboard engine
typio engine list        # list engines (* marks active)
typio engine next        # cycle to next engine
typio engine rime        # switch to rime
typio rime schema        # print current Rime schema
typio rime deploy        # rebuild generated Rime config files
typio rime schema luna_pinyin # set Rime schema
typio config reload      # reload config from disk
typio config get         # print current config text
typio status             # show server status summary
typio stop               # stop the daemon
typio version            # show server version
```

## Implementation notes

- The D-Bus adapter lives in `daemon/status/status.c`. It marshals
  D-Bus arguments into JSON params, calls
  `typio_status_service_handle()`, and converts the JSON response back
  into a D-Bus reply.
- The shared business logic lives in `daemon/ipc/status_service.c`.
- Protocol constants are in `core/include/typio/dbus_protocol.h`.
- `typio` client source lives under `cli/` (Rust) and uses UDS without
  depending on `typio-core`.
- `typio-control` (GTK4) still uses this D-Bus interface via GDBusProxy.
- The status bus integration test is in `tests/test_status_bus.c`.
