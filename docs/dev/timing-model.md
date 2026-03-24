# Timing Model

## Purpose

This document defines the timing model for Typio's Wayland input-method path.
It exists to keep event ordering, ownership, and cleanup rules explicit.

If a keyboard or focus bug appears "sometimes", treat it as a timing-model
problem first, not as a one-off key handling bug.

The most failure-sensitive chain today is:

1. `zwp_input_method_v2` activation or reactivation
2. keyboard-grab creation or rebuild
3. compositor keymap delivery on the grab
4. keymap forwarding into `zwp_virtual_keyboard_v1`
5. virtual-keyboard transition to `ready`
6. only then: unhandled key forwarding to the focused application

If that chain is incomplete or reordered, the frontend must not behave as if
virtual-keyboard forwarding is healthy.

## Lifecycle Phases

The frontend runs in one of four phases:

- `inactive`
- `activating`
- `active`
- `deactivating`

Rules:

- only `active` may process key or repeat events
- `activating` may process modifier events for the newly created keyboard grab
  so held Ctrl/Alt/Super state survives grab recreation before the first new
  key press
- `activate` moves the frontend to `activating` only when the current session
  is not already focused
- an `activate` received while the current session is still focused is treated
  as a deferred reactivation request and does not interrupt the in-flight key
  sequence
- `deactivate` moves the frontend to `deactivating`
- `done` is the reactivation commit point: deferred reactivation, if any, is
  applied there after the current key sequence has had a chance to finish
- successful focus-in plus keyboard-grab setup moves the frontend to `active`
- focus-out cleanup moves the frontend back to `inactive`

## Truth Sources

Each event category has one source of truth:

- `activate/deactivate/done`: lifecycle truth
- `key press/release`: physical key truth
- `modifiers`: modifier-mask truth
- `repeat_info/repeat timer`: repeat truth
- virtual keyboard output: side effect only, never the source of internal truth

Do not derive lifecycle truth from forwarded virtual-keyboard output.

Additional rule:

- a live keyboard grab is not proof that the virtual keyboard is ready
- a previously healthy virtual keyboard is not proof that the current grab has
  a current keymap

## Ownership

- `wl_input_method.c` owns lifecycle transitions
- `lifecycle.c` owns lifecycle validation and hard keyboard reset boundaries
- `wl_keyboard.c` owns key-event interpretation while the lifecycle is `active`
- `key_tracking.c` owns bulk key-state mutations used at lifecycle boundaries
- `startup_guard.*` owns startup-time suppression policy
- `boundary_bridge.*` owns boundary-handoff policy such as orphan-release
  cleanup and temporary VK modifier carry across deactivation
- `vk_bridge.*` owns virtual-keyboard health, keymap deadlines, readiness
  gating, and fail-safe downgrade
- `xkb_state` owns the logical modifier view
- `modifier_policy.*` owns effective event-modifier resolution
- engine implementations own only engine/composition behavior

The status D-Bus surface exports this state, but does not own it. Runtime
state reported through `RuntimeState` is a read-only snapshot of frontend
truth, not an independent source of truth.

## Virtual Keyboard State Machine

The Wayland frontend treats virtual-keyboard health as an explicit state
machine:

- `absent`: no usable virtual keyboard object is currently available
- `needs_keymap`: a virtual keyboard exists, but the current keyboard-grab
  generation has not finished the keymap handoff yet
- `ready`: the current generation has delivered a compositor keymap to the
  virtual keyboard and forwarding may proceed
- `broken`: the virtual keyboard path is considered unhealthy and must not be
  trusted for continued forwarding

Rules:

- grab rebuild must force `needs_keymap`
- old `ready` state must not survive into a new grab generation
- `ready` requires a compositor keymap observed in the current generation
- timeout while in `needs_keymap` is a fail-safe condition
- `broken` is a fail-safe condition

This is the critical upgrade from "implicit implementation flow" to
"constrained state machine". The code is no longer allowed to assume that
keymap arrival will happen eventually and harmlessly.

## Hard Reset Boundary

Cross-input-context switches are hard reset boundaries.

On a hard reset:

- forwarded keys are released to the virtual keyboard
- virtual keyboard modifiers are normally reset to zero
- exception: during `deactivating`, the last compositor-reported modifier mask
  may be carried to the virtual keyboard so the newly focused client can still
  observe a held shortcut modifier; this carried state must be cleared before
  the next activation begins
- key repeat is cancelled
- keyboard grab state is destroyed
- per-key tracking state is cleared
- new contexts start with no carried key sequence

This is intentionally strict. Old context key sequences are treated as
untrusted once focus changes.

For virtual-keyboard safety, hard-reset boundaries also imply:

- any stale assumption that vk is `ready` must be discarded
- the next forwarding generation must earn `ready` again through keymap sync

## Boundary Bridge Rules

`boundary_bridge.*` owns short-lived handoff exceptions at activation
boundaries.

Rules:

- orphan non-modifier releases may be forwarded to the virtual keyboard only as
  activation-boundary cleanup
- deactivation may temporarily carry the last compositor-reported modifier mask
  to the virtual keyboard for the newly focused client
- any carried modifier state must be reset before the next activation begins

## Shortcut Policy

Application shortcuts are decided in the Wayland frontend.

Rules:

- non-modifier keys with Ctrl, Alt, or Super bypass the engine
- the matching release must also bypass the engine
- engines should not each implement their own shortcut bypass policy
- `Ctrl+Shift` and similar modifier-only shortcuts are not Typio-owned input
  behavior and should remain transparent to the application/compositor path

## Invariants

- no key press/release events are processed outside the `active` phase
- modifier-mask updates may be processed in `activating` to resynchronize held
  modifiers before the lifecycle reaches `active`
- no virtual-keyboard forwarding happens unless vk is explicitly `ready`
- repeated `activate` events during an already focused session must not cut off
  a press/release pair that is already in flight
- no key tracking state survives a hard reset boundary
- bulk key-state rewrites happen only in lifecycle cleanup code
- application shortcut press/release must remain symmetric
- startup suppression must remember why a key was suppressed
- a rebuilt keyboard grab must not inherit prior-grab keymap health
- fail-safe paths must prefer releasing the keyboard grab over continuing to
  run in a partially broken forwarding state

## Test Expectations

At minimum, timing-model regressions should be covered by:

- lifecycle helper tests
- key-tracking boundary cleanup tests
- startup guard classification tests
- boundary bridge policy tests
- repeat guard tests
- virtual-keyboard state-machine tests covering `needs_keymap`, `ready`,
  `broken`, and keymap-timeout transitions

If a bug depends on a concrete sequence, add that sequence to tests in reduced
form rather than leaving it as a manual repro only.

## Runtime Observability

Current builds export a `RuntimeState` dictionary over the status D-Bus
surface. This is the preferred live view for timing bugs that only reproduce
under a real compositor session.

The highest-value fields for timing diagnosis are:

- `lifecycle_phase`
- `keyboard_grab_active`
- `virtual_keyboard_state`
- `virtual_keyboard_has_keymap`
- `virtual_keyboard_drop_count`
- `virtual_keyboard_state_age_ms`
- `virtual_keyboard_keymap_deadline_remaining_ms`

When a bug report says "Typio ran for a while and then input died", compare the
runtime snapshot against logs. A healthy active session should usually look
like:

- `lifecycle_phase=active`
- `keyboard_grab_active=true`
- `virtual_keyboard_state=ready`
- `virtual_keyboard_has_keymap=true`
- `virtual_keyboard_drop_count=0` or stable

If instead you see `keyboard_grab_active=true` with
`virtual_keyboard_state=needs_keymap`, treat that as a primary clue that the
grab-to-keymap-to-vk chain did not close properly.

## Trace Capture

For shortcut-routing or repeat bugs, run:

```sh
typio --verbose 2>&1 | tee typio-trace.log
```

Read traces in this order:

1. sort by `seq`
2. group by `topic`
3. compare `phase`, `keygen`, `activegen`, `mods`, `phys`, and `xkb`

For `Ctrl-T` style bugs, inspect `TRACE key`, `TRACE vk_key`, and
`TRACE vk_modifiers`. If a release shows `keygen != activegen`, treat it as an
activation-boundary orphan and check whether `boundary_bridge.*` classified it
for virtual-keyboard cleanup.
