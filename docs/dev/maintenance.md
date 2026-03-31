# Developer Maintenance Manual

## Purpose

This manual documents the maintenance rules for Typio's Wayland keyboard
pipeline. Its goal is to keep the implementation safe, robust, predictable,
and easy to extend without reintroducing stale-key bugs or asymmetric key
sequences.

Use this document together with [Architecture](architecture.md) when
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

If `activate` arrives while the current session is already focused, treat it as
a deferred reactivation request:

1. keep the current grab active long enough to finish any in-flight key cycle
2. record pending reactivation state only
3. wait for the matching `done`
4. recreate the keyboard grab at that commit point

Rule: do not move the frontend out of `active` just because a repeated
`activate` arrived for an already focused session.

On deactivation:

1. forwarded keys are force-released to the virtual keyboard
2. modifier carry may preserve the last compositor-reported modifier mask across
   the boundary for the newly focused client
3. repeat is cancelled
4. on focus loss in `done`, the keyboard grab is destroyed
5. per-key tracking state is reset

Rule: no `TypioKeyTrackState` value may survive from one activation to the next.

## Key Tracking State Machine

`TypioKeyTrackState` lives in `src/apps/daemon/wayland/wl_frontend_internal.h`.
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

The startup guard is intentionally split into two time-based policies:

- stale-key guard: a very short window that absorbs keys already held when a new grab starts
- Enter guard: a longer window that blocks accidental immediate submission when no composition is active

Classification is centralized in
`typio_wl_startup_guard_classify_press()`.

Rule: if a decision depends on "why" a key is suppressed, add that rule to the
classifier instead of duplicating time-window logic in `wl_keyboard.c`.

The startup guard does not own activation-boundary VK handoff decisions. Orphan
release cleanup and carried modifier behavior belong to `boundary_bridge.*`.

## Lifecycle Cleanup Boundary

`key_tracking.c` centralizes the lifecycle-only bulk operations on key state:

- reset all tracked keys when a grab is created or destroyed
- mark forwarded keys as `RELEASED_PENDING` only when Typio explicitly performs
  boundary cleanup during deactivation or grab teardown

Rule: bulk conversion of forwarded keys is a lifecycle operation, not a normal
modifier-path operation. If a change needs to rewrite many key states at once,
it belongs at a focus/grab boundary and should be implemented through this
helper layer.

`boundary_bridge.*` owns the policy decisions for short-lived bridge behavior at
activation boundaries:

- whether an orphan non-modifier release should be forwarded as cleanup
- whether carried virtual-keyboard modifiers must be reset
- whether deactivation may carry the compositor's last modifier mask across the
  boundary

Rule: keep boundary handoff decisions in `boundary_bridge.*`; callers may
execute the resulting VK or lifecycle action, but should not duplicate the
conditions inline.

Rule: virtual keyboard output is only valid for keys Typio actually owns in the
current generation. Do not use VK events to "repair" a key cycle whose press
did not pass through Typio.

Exception: at the `deactivate` boundary, Typio may carry the last compositor-
reported modifier mask to the virtual keyboard so a held Ctrl/Alt/Super can
still affect the newly focused client. That carried VK modifier state is a
short-lived bridge only and must be cleared before the next activation starts.

Exception: an orphan `Enter` release may be forwarded as activation-boundary
cleanup even without shortcut modifiers, because the application must not be
left with a stuck app-facing `Enter` press.

## Modifier Truth

Effective event modifiers are centralized in `modifier_policy.*`.

Rule:

- owned generations trust Typio's physical modifier tracking
- not-yet-owned generations may inherit Ctrl/Alt/Super from `xkb_state`
- Caps Lock and Num Lock come from `xkb_state`
- do not re-implement modifier fallback rules inline in `wl_keyboard.c`

## Shortcut Policy

The normative shortcut rules live in [Timing Model](timing-model.md). For
maintenance work, treat them as non-negotiable invariants:

- shortcut bypass policy belongs to the Wayland frontend, not per engine
- shortcut press/release handling must remain symmetric
- modifier-only compositor shortcuts such as `Ctrl+Shift` stay transparent
- cleanup rewrites belong at lifecycle boundaries, not in the normal key path

## Safe Change Checklist

Before merging keyboard-path changes, verify:

- activation and deactivation do not leave per-key state behind
- repeated `activate` during an already focused session does not interrupt an
  in-flight key press/release pair
- key repeat cancellation still works when modifiers change
- forwarded modifier shortcuts still reach applications
- engine-only handled keys do not leak releases to applications
- startup Enter suppression does not emit a lone app-facing release
- tests cover the new state transition or lifecycle boundary

## Required Tests

At minimum, keyboard-path changes should keep these areas covered:

- startup guard classification
- boundary bridge policy
- repeat cancellation helper logic
- activation-boundary reset of key tracking state
- stale key suppression and release cleanup
- Enter guard symmetry
- deferred reactivation commit rules

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
- app keeps repeating a shortcut letter after refocus: `boundary_bridge.*`
  likely missed an orphan-release VK cleanup decision
- a held Ctrl/Alt/Super stops affecting shortcuts after grab recreation: the
  new grab saw the modifier only via XKB/modifier events, not a fresh key
  press; physical modifier state must be resynchronized from XKB on modifier
  updates so later shortcuts still inherit the held modifier
- a held Ctrl/Alt/Super stops affecting the newly focused client after
  deactivation: verify whether the last compositor modifier mask was carried
  across the boundary and later reset before the next activation

## Documentation Update Policy

Any change that modifies one of these must update this manual:

- `TypioKeyTrackState`
- startup suppression policy
- activation/deactivation ordering
- synthetic release behavior
- keyboard repeat ownership

This keeps maintenance knowledge in-repo instead of in commit history only.
