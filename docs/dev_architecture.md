# Architecture

## Overview

Typio is split into a small core library and a Wayland-facing daemon. The
daemon directly implements the input-method side of the Wayland text
input stack and relies on the compositor/application `text-input-v3` path
for end-to-end text entry.

```text
Wayland compositor
        |
        v
   typio (daemon)
        |
        v
   typio-core
        |
   +----+------------------------------------------+
   |                                               |
built-in basic engine                 external plugin engines
                                          |
                                       rime plugin
```

## Protocol Stack

Typio runs on the following protocol layers:

- `zwp_text_input_manager_v3` / `zwp_text_input_v3`
  This is the application-to-compositor text input path. It carries editing
  state, surrounding text, content type, and cursor rectangle information.
  Typio relies on this path being present in the session, but Typio is not
  the text-input client.
- `zwp_input_method_manager_v2` / `zwp_input_method_v2`
  This is the compositor-to-input-method path. Typio directly implements
  this side to receive activation, surrounding-text state, and to send
  preedit and commit requests back through the compositor.
- `zwp_input_method_keyboard_grab_v2`
  This gives Typio the keyboard stream it needs for composition, candidate
  navigation, and command handling.
- `zwp_input_popup_surface_v2`
  This is the native Wayland candidate popup path used for input method UI
  placement near the active text cursor.
- `wl_compositor`, `wl_surface`, `wl_shm`
  These core Wayland interfaces back the popup window and its pixel buffers.

## Main Components

### `typio-core`

Located under `src/lib/`.

Responsibilities:

- instance lifecycle
- engine registration and loading
- input context state
- configuration parsing
- key and voice event structures
- shared utility code

### `typio`

Located under `src/server/`.

Responsibilities:

- connect to the Wayland display
- bind `zwp_input_method_manager_v2`
- operate within sessions where applications and the compositor expose `zwp_text_input_manager_v3`
- bind `wl_compositor` and `wl_shm` for popup candidate rendering
- create per-activation Typio sessions
- grab keyboard input through the input-method protocol
- translate XKB keyboard state into `TypioKeyEvent`
- forward commit and preedit callbacks back into Wayland protocol requests

### Built-In `basic` Engine

Located at `src/engines/basic/basic.c`.

Responsibilities:

- provide a zero-dependency built-in keyboard engine
- commit printable Unicode text directly
- give the daemon a usable default engine even when no plugins are installed

## Engine Manager Model

`TypioEngineManager` supports two engine sources:

- built-in engines registered at startup
- external shared objects loaded from the engine directory

For external engines, Typio expects exported symbols:

- `typio_engine_get_info`
- `typio_engine_create`

Each engine instance receives a config path such as:

```text
~/.config/typio/engines/<engine>.toml
```

For built-in integrations, Typio's authoritative user configuration now lives
in the root file `~/.config/typio/typio.toml`, typically under sections such
as `[engines.rime]` and `[engines.mozc]`.

## Wayland Data Flow

1. The compositor activates the input method.
2. Typio creates or resets a session.
3. Typio grabs the keyboard and builds XKB state.
4. Key presses become `TypioKeyEvent`.
5. The active engine returns one of:
   - not handled
   - handled internally
   - composing
   - committed
6. Commit and preedit callbacks are translated into `zwp_input_method_v2` requests.
7. Candidate lists are rendered through `zwp_input_popup_surface_v2` when the session exposes the necessary Wayland globals. If popup rendering is unavailable, Typio keeps candidate state visible inline in preedit.

## Keyboard Safety Model

The Wayland keyboard grab path uses an explicit per-key state machine for
forwarded keys, synthetic releases, and startup suppression. Activation-
boundary handoff policy is kept separate in `boundary_bridge.*`.

The intended forwarding model is conservative: if the IME does not consume a
key, Typio forwards the original press/release sequence and separately keeps
the virtual keyboard modifier state in sync. Modifier changes must not trigger
synthetic releases for unrelated non-modifier keys in the main key path.

The maintenance rules for this state machine live in
[Developer Maintenance Manual](dev_maintenance.md). Any change to keyboard
grab lifecycle or suppression behavior should update that document and the
[Timing Model](dev_timing_model.md) alongside the code.

## Current Scope

Implemented:

- Wayland input method frontend
- Wayland-native protocol stack based on `zwp_input_method_manager_v2` and compositor/application `zwp_text_input_manager_v3`
- keyboard grab and XKB integration
- commit/preedit callback bridge
- popup candidate surface rendering over pure Wayland protocol objects
- dynamic engine loading ABI
- built-in basic keyboard engine
- bundled `librime` engine plugin in the default build
- automated tests

Still limited in this repository:

- popup candidates are keyboard-driven; no pointer interaction layer is implemented
- richer compositor integration beyond the current input method protocol path

## Ownership Rules

- `TypioInstance` owns `TypioEngineManager`, `TypioConfig`, and created contexts.
- `TypioInputContext` owns its preedit, candidates, and property storage.
- `TypioWlFrontend` owns the Wayland connection, popup surface, current session, and keyboard grab.
- Engine implementations own their own `user_data`.
