# Timing Model

## Purpose

This document defines the timing model for Typio's Wayland input-method path.
It exists to keep event ordering, ownership, and cleanup rules explicit.

If a keyboard or focus bug appears "sometimes", treat it as a timing-model
problem first, not as a one-off key handling bug.

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

## Ownership

- `wl_input_method.c` owns lifecycle transitions
- `lifecycle.c` owns lifecycle validation and hard keyboard reset boundaries
- `wl_keyboard.c` owns key-event interpretation while the lifecycle is `active`
- `key_tracking.c` owns bulk key-state mutations used at lifecycle boundaries
- `startup_guard.*` owns startup-time suppression policy
- `boundary_bridge.*` owns boundary-handoff policy such as orphan-release
  cleanup and temporary VK modifier carry across deactivation
- `xkb_state` owns the logical modifier view
- `modifier_policy.*` owns effective event-modifier resolution
- engine implementations own only engine/composition behavior

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
- repeated `activate` events during an already focused session must not cut off
  a press/release pair that is already in flight
- no key tracking state survives a hard reset boundary
- bulk key-state rewrites happen only in lifecycle cleanup code
- application shortcut press/release must remain symmetric
- startup suppression must remember why a key was suppressed

## Test Expectations

At minimum, timing-model regressions should be covered by:

- lifecycle helper tests
- key-tracking boundary cleanup tests
- startup guard classification tests
- boundary bridge policy tests
- repeat guard tests

If a bug depends on a concrete sequence, add that sequence to tests in reduced
form rather than leaving it as a manual repro only.

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
