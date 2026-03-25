# Usage

## Basic Commands

```bash
typio --list
typio --version
typio --help
typio --engine basic --verbose
typio --engine-dir ~/.local/lib/typio/engines --engine rime --verbose
```

Important options:

- `--config DIR`
- `--data DIR`
- `--engine-dir DIR`
- `--engine NAME`
- `--list`
- `--verbose`
- `--version`

## Running the Daemon

Start Typio inside a Wayland session:

```bash
typio
```

For debugging:

```bash
typio --verbose
```

To run against a non-installed plugin directory:

```bash
typio --engine-dir ./build/engines --engine rime --verbose
```

If the session exposes the Wayland input-method/text-input stack and no other
input method currently owns it, Typio will stay in the foreground and process
Wayland events.

## D-Bus Status Interface

Typio exports structured runtime state on the session bus at:

- service: `org.typio.InputMethod1`
- path: `/org/typio/InputMethod1`
- interface: `org.typio.InputMethod1`

Read-only properties:

- `Version`
- `ActiveEngine`
- `AvailableEngines`
- `ActiveEngineState`
- `ConfigText`

Methods:

- `ActivateEngine(s)`
- `SetConfigText(s)`
- `ReloadConfig()`

`ActiveEngineState` is an `a{sv}` map containing stable engine metadata plus
top-level engine config keys prefixed as `config.*`, such as
`config.shared_data_dir` for Rime. The active Rime schema is exposed
separately through the `RimeSchema` property. Typio emits the standard
`org.freedesktop.DBus.Properties.PropertiesChanged` signal when these values
change, so richer shells and widgets can react without relying on tray icon
changes alone.

## GTK4 Control Panel

If Typio was built with `-DBUILD_CONTROL_PANEL=ON`, the build tree also
contains:

```bash
./build/src/control/typio-control
```

The control panel reads Typio state from `org.typio.InputMethod1` and can:

- switch the active keyboard engine
- edit appearance settings such as popup theme, candidate layout, and font size
- edit notification settings
- configure engine-specific options such as the active Rime schema or Mozc page size
- pick the voice backend and installed model
- record shortcut bindings directly from the keyboard

The current layout is split into three top-level pages:

- `Appearance`
- `Engines`
- `Shortcuts`

The control panel follows an instant-apply model:

- widget changes take effect in the UI immediately
- Typio saves them automatically in the background
- successful saves are silent
- temporary retries are silent
- only real save failures surface a short inline error message

This means there is no global `Apply` / `Cancel` flow in the current UI.

## Built-In `basic` Engine

The built-in engine is the baseline keyboard engine. It:

- handles printable key presses
- converts them to UTF-8
- commits them directly to the focused client
- ignores control-style combinations such as `Ctrl`, `Alt`, and `Super`

It does not provide:

- candidate UI
- composition windows
- speech input
- advanced language logic

That functionality is expected to come from external engines.

## `rime` Engine

In the default build, Typio loads the `rime` plugin for Chinese input. Typical local testing from the build tree looks like this:

```bash
./build/src/server/typio --engine-dir ./build/engines --engine rime --verbose
```

On Wayland, Typio renders Rime candidates through a dedicated input-method
popup surface. If popup rendering is unavailable in the current session,
Typio keeps candidate state visible inline in preedit. Candidate selection
still uses the engine's normal selection keys.

The popup follows Wayland surface enter/leave events and `wl_output.scale`.
That keeps integer-scaled outputs such as 2x crisp instead of stretching a 1x
buffer. Fractional scaling is still compositor-dependent because Typio does
not yet use an additional fractional-scale extension protocol.

The popup defaults to a horizontal candidate layout. You can override it in
`typio.toml` under `[engines.rime]` with `candidate_layout = "vertical"`.
Popup colors can be forced with `popup_theme = "light"` or
`popup_theme = "dark"`; otherwise `popup_theme = "auto"` uses desktop theme
hints when available.

After installation, `typio --list` will show `rime` automatically if the plugin was installed into the configured engine directory.

## Runtime Expectations

- Typio currently targets Wayland only.
- Typio directly implements `zwp_input_method_manager_v2`.
- The session must provide a working `zwp_text_input_manager_v3` path between applications and the compositor.
- Only one input method can own the Wayland input method seat at a time.
- External engines appear in `typio --list` once their shared objects are installed in the engine directory or passed with `--engine-dir`.
- Candidate popups use the Wayland input-method popup-surface protocol path and do not rely on X11 or xdg-shell.
