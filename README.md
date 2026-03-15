# Typio

Typio is a native Wayland input method daemon written in C. It runs on the
Wayland text-input/input-method protocol stack and provides:

- `typio-core`: the engine, context, config, and plugin runtime
- `typio`: a `zwp_input_method_v2` Wayland frontend
- `basic`: a built-in keyboard engine that commits printable text directly
- `rime`: the default Chinese input engine plugin backed by `librime`
- a shared-library engine ABI for richer out-of-tree engines
- a pure-Wayland candidate popup surface for engines that expose candidates
- a D-Bus status interface for structured runtime state beyond what a tray icon
  can express

## Protocol Stack

Typio is designed around the native Wayland text input protocol stack:

- `zwp_text_input_manager_v3` / `zwp_text_input_v3`
  Applications use this path to tell the compositor about editable state,
  cursor rectangles, surrounding text, and content type. Typio depends on
  this path, but does not act as the text-input client itself.
- `zwp_input_method_manager_v2` / `zwp_input_method_v2`
  Typio directly implements this side. The compositor activates the input
  method, forwards input-method state, and receives preedit or committed
  text back from Typio.
- `zwp_input_method_keyboard_grab_v2`
  Typio uses the keyboard grab to receive raw key events for composition,
  candidate selection, and command handling.
- `zwp_input_popup_surface_v2`
  Typio uses the popup-surface path for candidate UI placement near the
  active text cursor.
- `wl_compositor`, `wl_surface`, and `wl_shm`
  These Wayland core objects carry the actual candidate window buffers once
  popup rendering is active.

## Quick Start

Build and test:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Build with the optional GTK4 control panel:

```bash
cmake -S . -B build -DBUILD_CONTROL_PANEL=ON
cmake --build build
./build/src/control/typio-control
```

Run directly from the build tree (no install needed):

```bash
./build/src/server/typio --engine-dir ./build/engines --engine rime --verbose
```

Install is only needed for packaging or verifying the installed layout:

```bash
# Staging install (disposable, no root)
cmake --install build --prefix /tmp/typio-staging
/tmp/typio-staging/bin/typio --verbose
rm -rf /tmp/typio-staging

# System-wide install
sudo cmake --install build --prefix /usr/local
```

Runtime requirements:

- you must be inside a Wayland session
- the compositor must expose `zwp_input_method_manager_v2`
- applications and the compositor must provide a working `zwp_text_input_manager_v3` path
- no other input method daemon can already own the seat

## Install

Installed artifacts:

- `bin/typio`
- `lib/libtypio-core.so` or `lib/libtypio-core.a`
- `lib/typio/engines/rime.so`
- `include/typio/*.h`
- `lib/pkgconfig/typio.pc`
- `share/typio/typio.toml.example`

## Documentation

- [Documentation Index](docs/README.md)
- [User Installation](docs/user_installation.md)
- [User Configuration](docs/user_configuration.md)
- [User Usage](docs/user_usage.md)
- [Troubleshooting](docs/user_troubleshooting.md)
- [Developer Building Guide](docs/dev_building.md)
- [Architecture](docs/dev_architecture.md)
- [Developer Maintenance Manual](docs/dev_maintenance.md)
- [Developer Timing Model](docs/dev_timing_model.md)
- [Creating Engines](docs/dev_creating_engines.md)

## Notes

- Typio currently targets Wayland only.
- Default CMake options are used unless a command shows a `-D...` override.
- The default build includes the `rime` plugin.
- If you want a smaller build without Rime, configure with `-DBUILD_RIME_ENGINE=OFF`.
- Tray support is a separate compile-time feature. Disable it with `-DENABLE_SYSTRAY=OFF`.
- The session D-Bus status/control interface is also a separate feature. Disable it with `-DENABLE_STATUS_BUS=OFF`.
- The optional GTK4 control panel is built with `-DBUILD_CONTROL_PANEL=ON`.
- Only one input method can own the Wayland input-method seat at a time.
- Typio directly implements the Wayland input-method side and depends on the compositor/application text-input-v3 path for end-to-end text input.
- The built-in `basic` engine does not provide candidate UI.
- The `rime` engine renders candidates through `zwp_input_popup_surface_v2`. If popup rendering is unavailable in the current session, Typio keeps candidate state visible inline in preedit.
- The Rime popup defaults to a horizontal layout and can follow common desktop light/dark theme hints, with `typio.toml` overrides under `[engines.rime]` when needed.
- Tray hosts that ignore themed icon paths can still render the current engine icon through the exported `IconPixmap` fallback.
- When the active engine is `rime`, the tray menu exposes a dedicated submenu with the current schema and schema-switch actions.
- Typio also exports a D-Bus status object at `org.typio.InputMethod1` so shells
  such as quickshell can read the active engine, available engines, and
  engine/config state as structured properties instead of inferring everything
  from tray icon changes.
- Typio supports a single user-facing config file: `~/.config/typio/typio.toml`.
- Build-tree plugin testing is supported with `typio --engine-dir <build-dir>/engines`.
- The pre-`typio-core` prototype API and examples were removed; the maintained public headers now live under `src/lib/typio/`.
