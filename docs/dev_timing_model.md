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

- only `active` may process key, modifier, or repeat events
- `activate` moves the frontend to `activating`
- `deactivate` moves the frontend to `deactivating`
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
- `xkb_state` owns the logical modifier view
- `modifier_policy.*` owns effective event-modifier resolution
- engine implementations own only engine/composition behavior

## Hard Reset Boundary

Cross-input-context switches are hard reset boundaries.

On a hard reset:

- forwarded keys are released to the virtual keyboard
- virtual keyboard modifiers are explicitly reset to zero
- key repeat is cancelled
- keyboard grab state is destroyed
- per-key tracking state is cleared
- new contexts start with no carried key sequence

This is intentionally strict. Old context key sequences are treated as
untrusted once focus changes.

## Shortcut Policy

Application shortcuts are decided in the Wayland frontend.

Rules:

- non-modifier keys with Ctrl, Alt, or Super bypass the engine
- the matching release must also bypass the engine
- engines should not each implement their own shortcut bypass policy
- `Ctrl+Shift` and similar modifier-only shortcuts are not Typio-owned input
  behavior and should remain transparent to the application/compositor path

## Invariants

- no key events are processed outside the `active` phase
- no key tracking state survives a hard reset boundary
- bulk key-state rewrites happen only in lifecycle cleanup code
- application shortcut press/release must remain symmetric
- startup suppression must remember why a key was suppressed

## Test Expectations

At minimum, timing-model regressions should be covered by:

- lifecycle helper tests
- key-tracking boundary cleanup tests
- startup guard classification tests
- repeat guard tests

If a bug depends on a concrete sequence, add that sequence to tests in reduced
form rather than leaving it as a manual repro only.

## Trace Capture

For shortcut-routing or repeat bugs, run Typio with `--verbose` and capture
stderr. The keyboard path emits `TRACE` lines that include:

- monotonic per-frontend sequence number (`seq`)
- trace topic (`topic`)
- lifecycle phase
- keycode and keysym
- resolved Unicode codepoint and readable character when available
- current key route
- event modifiers
- physical modifier state
- XKB-derived modifier state
- key generation vs active generation
- whether the event was sent to the engine or virtual keyboard

Recommended capture command:

```sh
typio --verbose 2>&1 | tee typio-trace.log
```

For a `Ctrl-T` style repro, inspect:

- `TRACE key stage=dispatch-press`
- `TRACE key stage=press-forward`
- `TRACE key stage=release-orphan`
- `TRACE vk_key`

Example:

```text
TRACE seq=14 phase=active topic=key stage=dispatch-press keycode=28 keysym=0x74 keyname=t ... unicode=U+0074 char='t' ...
```

Recommended reading order when debugging:

1. sort by `seq`
2. group by `topic`
3. compare `phase`, `keygen`, and `activegen`

If the initial letter key is not classified as `app_shortcut`, the bug is in
route classification. If a later release shows `keygen != activegen`, Typio did
not own that key cycle in the current grab and the event should be treated as
an orphan. If Typio owns the key cycle and the client still receives repeating
text, the bug is downstream of Typio.

One important exception: if a non-modifier orphan release arrives inside the
startup stale-key window, it may be the missing cleanup release for a key press
that reached the application before the new grab existed. In that case Typio
should forward a virtual-keyboard release instead of consuming it silently, or
the client may keep repeating the held letter after a shortcut such as `Ctrl-T`.
