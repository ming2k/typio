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
   typio (daemon) ---- D-Bus (org.typio.InputMethod1)
        |                       |
        v                  +----+----+
   typio-core              |         |
        |            typio-client  typio-control
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
  Popup scaling follows `wl_surface.enter/leave` plus per-output
  `wl_output.scale` so the shm buffer matches integer HiDPI outputs.

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
- bind `wl_compositor` and `wl_shm` for candidate popup rendering
- create per-activation Typio sessions
- grab keyboard input through the input-method protocol
- translate XKB keyboard state into `TypioKeyEvent`
- forward commit and preedit callbacks back into Wayland protocol requests

Within the Wayland daemon, responsibilities are intentionally split by layer:

- `wl_input_method.c`
  Owns protocol-facing text entry updates and decides when the focused
  application needs a preedit round-trip.
- `text_ui_backend.c`
  Owns the backend boundary for Typio-managed text UI. It receives the
  current `TypioInputContext` state and forwards it to a concrete UI
  implementation.
- `candidate_popup.c`
  Implements the current Wayland-native popup backend over
  `zwp_input_popup_surface_v2`.
- `wl_event_loop.c`
  Owns the polling loop, Wayland dispatch, watchdog staging, and aux-fd
  integration such as tray, status bus, voice, repeat, and config watch.
- `wl_runtime_config.c`
  Owns runtime config reload, shortcut refresh, and text-UI config invalidation.
- `wl_frontend.c`
  Owns frontend construction, registry/global binding, and teardown glue.

### `typio-client`

Located under `src/client/`.

Responsibilities:

- provide a CLI for querying and controlling a running Typio daemon
- communicate exclusively over the `org.typio.InputMethod1` D-Bus interface
- no dependency on `typio-core`; pure D-Bus client

See [D-Bus Interface Reference](../reference/dbus-interface.md) for the full
protocol specification.

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

## Engine Categories

Typio models input engines in two parallel categories:

- `keyboard`
  The primary input pipeline. Keyboard engines own key processing, preedit,
  candidate lists, commits, and status icons.
- `voice`
  A secondary pipeline for speech recognition. Voice engines do not replace the
  active keyboard engine; they run alongside it and are selected independently.

Operational rules:

- there is exactly one active keyboard engine slot
- there is exactly one active voice engine slot
- keyboard and voice selections do not evict each other
- the tray, status bus, and control panel should treat keyboard and voice as
  separate runtime values, not as one flat engine list

The built-in `basic` engine is the baseline keyboard engine. Voice backends
such as Whisper and sherpa-onnx belong to the voice category.

## IME / Engine Boundary

Typio is the IME host and framework layer, not a replacement for engine-owned
language logic.

Authority split:

- engines own linguistic behaviour and engine-specific semantics
- Typio owns protocol hosting, UI integration, and cross-engine control surfaces

In practice, engine ownership includes:

- composition and conversion behaviour
- candidate generation, ordering, selection semantics, and paging
- engine-specific runtime state such as active schema or input mode
- any behaviour that only the upstream engine can define authoritatively

In practice, Typio ownership includes:

- Wayland input-method and popup-surface integration
- `TypioInputContext` as the transport and UI state carrier
- candidate popup rendering, tray/status export, and control-panel plumbing
- Typio-owned persisted config and runtime state publication
- converging user experience where presentation can be standardized without
  changing engine semantics

Design rules:

- Typio should respect the upstream engine's supported behaviour instead of
  reinterpreting it locally.
- Typio should prefer official engine APIs, runtime controls, and discovery
  paths over file-level hacks or private config rewrites.
- Typio may normalize presentation and workflow across engines, but it must
  not fake unsupported engine semantics just to make engines look identical.
- If an engine does not expose a supported control, preserving the engine's
  real behaviour is preferred over adding a Typio-side override that would
  contradict user expectations or upstream design intent.

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
7. Candidate lists are rendered through `zwp_input_popup_surface_v2` when the session exposes the necessary Wayland globals. If candidate popup rendering is unavailable, Typio keeps candidate state visible inline in preedit.

## Candidate Popup Pipeline

The candidate-list UI is intentionally layered so state and rendering stay
separate:

1. the keyboard engine owns candidate content and the selected index
2. `TypioInputContext` stores that state as the UI source of truth
3. `wl_input_method.c` decides whether the update is:
   - a full text-UI update, or
   - a selection-only popup refresh
4. `text_ui_backend.c` provides the Typio-side UI backend boundary
5. `candidate_popup.c` renders the current Wayland popup backend over
   `zwp_input_popup_surface_v2`

The important design rule is that `wl_input_method.c` should depend on the
text-UI backend abstraction, not on a concrete popup implementation. This
keeps candidate/preedit semantics in the input-method path while letting the
actual presentation backend evolve independently.

Selection-only movement such as `Up` / `Down` is treated as a hot path:

- it should not rebuild preedit when only the highlight changes
- it should reuse cached popup layout and row bitmaps whenever possible
- a transient popup redraw failure should keep the previous visible frame
  instead of hiding the candidate window

Current scheduling is intentionally asymmetric:

- when preedit text or cursor changes, Typio updates the popup and sends the
  new preedit to the focused application in the same call path
- when only the selected candidate changes, Typio still updates the popup
  synchronously in the same call path, but skips the redundant preedit
  protocol round-trip to the application

The short timing difference is:

1. `Up` / `Down` arrives
2. the engine updates the selected candidate
3. `update_wayland_text_ui()` calls `typio_wl_text_ui_backend_update()`
4. if preedit changed, Typio also sends `zwp_input_method_v2.set_preedit_string`
   and `commit`; if preedit did not change, that protocol work is skipped
5. the popup `wl_surface` is committed with the new highlight in the same turn

This separation is important because candidate ownership belongs to the engine
and input context, while popup buffers, damage, scaling, and backend-specific
surface management belong to the Wayland text-UI implementation.

## Keyboard Safety Model

The Wayland keyboard grab path uses an explicit per-key state machine for
forwarded keys, synthetic releases, and startup suppression. Activation-
boundary handoff policy is kept separate in `boundary_bridge.*`.

The intended forwarding model is conservative: if the IME does not consume a
key, Typio forwards the original press/release sequence and separately keeps
the virtual keyboard modifier state in sync. Modifier changes must not trigger
synthetic releases for unrelated non-modifier keys in the main key path.

The maintenance rules for this state machine live in
[Developer Maintenance Manual](maintenance.md). Any change to keyboard
grab lifecycle or suppression behavior should update that document and the
[Timing Model](timing-model.md) alongside the code.

## Current Scope

Implemented:

- Wayland input method frontend
- Wayland-native protocol stack based on `zwp_input_method_manager_v2` and compositor/application `zwp_text_input_manager_v3`
- keyboard grab and XKB integration
- commit/preedit callback bridge
- candidate popup surface rendering over pure Wayland protocol objects
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

For persisted config vs runtime-state ownership across daemon and control
surfaces, see [State Management](state-management.md).
