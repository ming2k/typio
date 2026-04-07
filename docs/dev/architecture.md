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

## Source Tree

The source tree is organized by stable product boundary first:

- `src/core/`
  Shared library code. `include/typio/` holds the public C headers,
  `runtime/` holds the core implementation, and `utils/` holds internal
  support code used by the runtime.
- `src/apps/`
  Executable programs. `daemon/` is the Wayland IME host, `cli/` is the
  D-Bus command-line control surface, and `control/` is the GTK control panel.
- `src/engines/`
  Built-in and pluggable input-engine implementations.

This keeps top-level `src/` directories on one axis: reusable core,
user-facing applications, and engine implementations.

## Main Components

### `typio-core`

Located under `src/core/`.

Responsibilities:

- instance lifecycle
- engine registration and loading
- input context state
- configuration parsing
- key and voice event structures
- shared utility code

Internal split:

- `src/core/include/typio/`
  Installed public headers and cross-component protocol constants
- `src/core/runtime/`
  Core implementation units that build `typio-core`
- `src/core/utils/`
  Internal support helpers used by the core, daemon, engines, and selected
  tests

### `typio`

Located under `src/apps/daemon/`.

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
  `zwp_input_popup_surface_v2`.  A `PopupDelta` classifier drives render-path
  selection (selection-only, aux-only, content, or style rebuild).
- `candidate_popup_layout.c`
  Owns text measurement and geometry computation.  A persistent `PopupPangoCtx`
  holds a 64-entry LRU cache of `PangoLayout` objects keyed by formatted
  candidate text and font description, so layouts survive page changes and avoid
  repeated font shaping.  `PopupGeometry` is an immutable snapshot of all
  computed positions; the selected index is deliberately excluded from it.
- `candidate_popup_paint.c`
  Owns Cairo pixel rendering for all three render paths, each accepting a
  `PopupGeometry*` rather than a long parameter list.
- `key_route.c`
  Owns key-routing decisions. The current model separates final action
  (`consume` or `forward`) from routing reason (for example reserved Typio
  shortcut, application shortcut, engine handled, or engine unhandled).
  This is intentionally separate from per-key tracking state such as
  `TRACK_FORWARDED` or `TRACK_APP_SHORTCUT`, which exists to keep later release cleanup
  symmetric.
- `wl_keyboard.c`
  Owns keyboard-grab event handling, XKB updates, and the highest-priority
  emergency-exit fast path before normal routing.
- `wl_event_loop.c`
  Owns the polling loop, Wayland dispatch, watchdog staging, and aux-fd
  integration such as tray, status bus, voice, repeat, and config watch.
- `wl_runtime_config.c`
  Owns runtime config reload, shortcut refresh, and text-UI config invalidation.
- `wl_frontend.c`
  Owns frontend construction, registry/global binding, and teardown glue.

Observability ownership follows the same boundary split:

- `wl_input_method.c`
  Owns lifecycle-edge logs for `activate`, `deactivate`, `done`, focus-in, and
  focus-out decisions.
- `lifecycle.c`
  Owns hard-reset boundary logs and the reason a keyboard generation is torn
  down.
- `vk_bridge.*`
  Owns virtual-keyboard health logs: state transitions, keymap wait/timeout,
  drop escalation, and fail-safe stop reasons.
- `wl_keyboard.c`
  Owns per-event keyboard and modifier sequencing traces, but these should stay
  in `debug` unless an invariant is violated.
- `wl_event_loop.c`
  Owns watchdog, poll/dispatch, and display-connection failure logs.
- D-Bus status export
  Owns `RuntimeState` publication only. It mirrors frontend truth for live
  inspection and must not invent or reinterpret event history.

Design rules for logging:

- one boundary owner should emit the primary `info` summary for a state change
- helper layers may add `debug` detail, but should not duplicate the same
  high-level event at `info`
- repeated high-frequency paths should prefer `debug` traces plus aggregated
  `warning` summaries over per-event `info` spam
- `RuntimeState` is the authoritative live snapshot, while logs are the ordered
  event history used to explain how the frontend reached that snapshot

This document only defines the structural split. Detailed timing rules,
runtime-state authority, and control-surface binding rules live in:

- [Timing Model](timing-model.md)
- [State Management](state-management.md)
- [Control Surfaces](control-surfaces.md)

### `typio-client`

Located under `src/apps/cli/`.

Responsibilities:

- provide a CLI for querying and controlling a running Typio daemon
- communicate exclusively over the `org.typio.InputMethod1` D-Bus interface
- no dependency on `typio-core`; pure D-Bus client

### `typio-control`

Located under `src/apps/control/`.

Responsibilities:

- provide a GTK control panel for runtime state and persistent configuration
- consume the same D-Bus surface as `typio-client`
- reuse `typio-core` config and schema helpers where shared parsing logic is
  preferable to duplicating it in UI code

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
3. `wl_input_method.c` decides when text UI must be refreshed
4. `text_ui_backend.c` provides the Typio-side UI backend boundary
5. `candidate_popup.c` classifies the change and dispatches to the correct
   render path over `zwp_input_popup_surface_v2`

The important architectural rule is that `wl_input_method.c` depends on the
text-UI backend abstraction, not on a concrete popup implementation. Timing
details for synchronous candidate refresh and related hot-path constraints are
documented in [Timing Model](timing-model.md).

### Delta classification

Every update is first classified into one of five `PopupDelta` values before
any rendering work begins:

| Delta | Trigger | Action |
|-------|---------|--------|
| `NONE` | Nothing visible changed | Skip rendering |
| `SELECTION` | Only selected index changed | Repaint two rows only |
| `AUX` | Only preedit / mode label changed (same popup size) | Repaint aux area only |
| `CONTENT` | Candidate list changed (page navigation) | Full repaint |
| `STYLE` | Font, theme, or output scale changed | Cache invalidation + full repaint |

Classification is a pure comparison of the incoming state against the cached
`PopupGeometry` snapshot and costs no rendering work.

### Geometry and layout cache

`PopupGeometry` is an immutable snapshot of all computed candidate positions and
auxiliary text positions for one page. The selected index is **not** part of the
geometry; changing the selection never requires re-measuring text or recomputing
positions.

Text measurement and `PangoLayout` objects are owned by `PopupPangoCtx`, a
persistent per-popup structure holding a 64-entry LRU cache. Cache entries are
keyed by `FNV-1a(formatted_text + font_desc)`. Layouts are created directly
from a `PangoContext` — no scratch Cairo surface is needed for measurement.
`pango_cairo_update_layout()` is called during painting to adapt metrics to the
scaled Cairo context.

On a typical page change, most candidate layouts are already in the cache
because the user has seen those candidates in a previous session or because the
same characters recur across adjacent pages. Cold-cache layout creation involves
HarfBuzz text shaping; warm-cache lookup is a hash comparison and a `lru_tick`
bump.

### Paint paths

`candidate_popup_paint.c` implements three paint functions, each accepting a
`PopupGeometry*`:

1. `popup_paint_full`
   Full background, border, all candidate rows, preedit, and mode label.
   Used for `CONTENT` and `STYLE` deltas.
2. `popup_paint_selection`
   Copies the last committed buffer, then repaints only the old and new
   selected rows. Used for `SELECTION` delta.
3. `popup_paint_aux`
   Copies the last committed buffer, erases the old preedit and mode-label
   regions, and draws the new ones. Used for `AUX` delta when popup dimensions
   are unchanged; falls back to `popup_paint_full` otherwise.

The safety rule is strict: partial repaint paths may reuse the last committed
buffer, but they must not change popup geometry or buffer-pool ownership
semantics. If any precondition is not met the implementation falls back to
`popup_paint_full`.

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
