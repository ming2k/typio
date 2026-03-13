# Developer Maintenance Manual

## Purpose

This manual documents the maintenance rules for Typio's Wayland keyboard
pipeline. Its goal is to keep the implementation safe, robust, predictable,
and easy to extend without reintroducing stale-key bugs or asymmetric key
sequences.

Use this document together with [Architecture](dev_architecture.md) when
changing keyboard handling, engine interaction, or focus lifecycle code.

## Design Goals

The keyboard path must preserve these properties:

- every forwarded key press must eventually produce exactly one forwarded release
- synthetic releases must never leak into a later activation
- startup suppression must distinguish between stale held keys and newly pressed keys
- key tracking state must be reset on activation boundaries
- engine-facing events and app-facing events must stay symmetric unless a rule explicitly says otherwise
- modifier transitions must not rewrite the press/release sequence of unrelated non-modifier keys

## Activation Lifecycle

The current lifecycle is:

1. `zwp_input_method_v2.activate`
2. session reset
3. `done`
4. `focus_in`
5. old keyboard grab destroyed if present
6. new keyboard grab created

On deactivation:

1. forwarded keys are force-released to the virtual keyboard
2. repeat is cancelled
3. on focus loss in `done`, the keyboard grab is destroyed
4. per-key tracking state is reset

Rule: no `TypioKeyTrackState` value may survive from one activation to the next.

## Key Tracking State Machine

`TypioKeyTrackState` lives in `src/server/wayland/wl_frontend_internal.h`.
Per-key ownership also tracks a `key_generation`: a key cycle belongs to the
current grab only if Typio observed its press in the current generation.

States:

- `TYPIO_KEY_IDLE`: no outstanding forwarded or suppressed state
- `TYPIO_KEY_FORWARDED`: press was forwarded to the application
- `TYPIO_KEY_APP_SHORTCUT`: non-modifier shortcut key intentionally bypassed the engine and was forwarded directly to the application
- `TYPIO_KEY_RELEASED_PENDING`: Typio already sent a synthetic release and must consume the physical one
- `TYPIO_KEY_SUPPRESSED_STARTUP`: stale held key from a previous grab
- `TYPIO_KEY_SUPPRESSED_ENTER`: a new Enter press blocked by the startup Enter guard

Maintenance rules:

- only `TYPIO_KEY_FORWARDED` may produce forwarded releases during normal key-up
- `TYPIO_KEY_APP_SHORTCUT` must forward both press and release without involving the engine
- `TYPIO_KEY_RELEASED_PENDING` must be cleared by the matching physical release or by activation reset
- `TYPIO_KEY_SUPPRESSED_STARTUP` may forward only a cleanup release
- `TYPIO_KEY_SUPPRESSED_ENTER` must consume its release locally and must not forward it
- do not synthesize releases for forwarded non-modifier keys just because Ctrl, Alt, or Super changed
- a release for a key whose press never reached Typio in the current generation
  is an orphan release and must be consumed locally, not sent to the engine or app

Do not merge `SUPPRESSED_STARTUP` and `SUPPRESSED_ENTER` back into one state.
They have different ownership and release semantics.

## Startup Guard Rules

The startup guard is intentionally split into two policies:

- stale-key guard: a very short window that absorbs keys already held when a new grab starts
- Enter guard: a longer window that blocks accidental immediate submission when no composition is active

Classification is centralized in
`typio_wl_startup_guard_classify_press()`.

Rule: if a decision depends on "why" a key is suppressed, add that rule to the
classifier instead of duplicating time-window logic in `wl_keyboard.c`.

## Lifecycle Cleanup Boundary

`key_tracking.c` centralizes the lifecycle-only bulk operations on key state:

- reset all tracked keys when a grab is created or destroyed
- mark forwarded keys as `RELEASED_PENDING` only when Typio explicitly performs
  boundary cleanup during deactivation or grab teardown

Rule: bulk conversion of forwarded keys is a lifecycle operation, not a normal
modifier-path operation. If a change needs to rewrite many key states at once,
it belongs at a focus/grab boundary and should be implemented through this
helper layer.

Rule: virtual keyboard output is only valid for keys Typio actually owns in the
current generation. Do not use VK events to "repair" a key cycle whose press
did not pass through Typio.

## Modifier Truth

Effective event modifiers are centralized in `modifier_policy.*`.

Rule:

- owned generations trust Typio's physical modifier tracking
- not-yet-owned generations may inherit Ctrl/Alt/Super from `xkb_state`
- Caps Lock and Num Lock come from `xkb_state`
- do not re-implement modifier fallback rules inline in `wl_keyboard.c`

## Shortcut Policy

Application shortcuts are decided in the Wayland frontend, not per engine.
Compositor-style modifier shortcuts such as `Ctrl+Shift` should remain
application/compositor-owned behavior unless an explicit out-of-band
integration is added.

Rule:

- non-modifier keys pressed with Ctrl, Alt, or Super bypass the engine
- the matching release must bypass the engine too
- engines should not need ad-hoc shortcut bypass logic to stay symmetric
- Typio should not claim `Ctrl+Shift` or similar modifier-only shortcuts from
  inside the keyboard grab
- modifier changes must not synthesize releases for unrelated keys during the
  normal key path; cleanup belongs at lifecycle boundaries only

## Safe Change Checklist

Before merging keyboard-path changes, verify:

- activation and deactivation do not leave per-key state behind
- key repeat cancellation still works when modifiers change
- forwarded modifier shortcuts still reach applications
- engine-only handled keys do not leak releases to applications
- startup Enter suppression does not emit a lone app-facing release
- tests cover the new state transition or lifecycle boundary

## Required Tests

At minimum, keyboard-path changes should keep these areas covered:

- startup guard classification
- repeat cancellation helper logic
- activation-boundary reset of key tracking state
- stale key suppression and release cleanup
- Enter guard symmetry

If a change touches `wl_keyboard.c` and cannot be covered by an existing helper
test, add a new helper or state-policy test rather than relying only on manual
testing.

## Debugging Workflow

When investigating swallowed or duplicated keys:

1. enable debug logging
2. capture the sequence of `press`, `release`, `modifiers`, and focus events
3. record the per-key state transition
4. verify whether the key crossed an activation boundary
5. check whether the key was classified as stale, Enter-guarded, forwarded, or force-released

Typical signatures:

- first press after refocus disappears: likely stale `RELEASED_PENDING` state
- app receives release without prior press: wrong suppression-state release handling
- Ctrl-based shortcut behaves oddly: main-path key sequence is being rewritten instead of only syncing modifiers
- app keeps repeating a shortcut letter after refocus: the application saw the
  old `press`, but Typio consumed the matching `release` after the activation
  boundary; stale orphan releases in the startup window must be forwarded as
  cleanup releases to the virtual keyboard for non-modifier keys
- a held Ctrl/Alt/Super stops affecting shortcuts after grab recreation: the
  new grab saw the modifier only via XKB/modifier events, not a fresh key
  press; physical modifier state must be resynchronized from XKB on modifier
  updates so later shortcuts still inherit the held modifier

## Documentation Update Policy

Any change that modifies one of these must update this manual:

- `TypioKeyTrackState`
- startup suppression policy
- activation/deactivation ordering
- synthetic release behavior
- keyboard repeat ownership

This keeps maintenance knowledge in-repo instead of in commit history only.
