# ADR-0003: Keyboard grab lifecycle

- **Status**: Accepted
- **Date**: 2026-05-22
- **Deciders**: Project maintainers

## Context

To compose text, navigate candidates, and handle shortcuts, the daemon takes a
`zwp_input_method_keyboard_grab_v2` and receives the raw key stream. This is the
single most dangerous surface in the project: while the grab is held, a logic
error can swallow or misroute keys, and a stuck grab can leave the user unable
to type *anywhere*. The grab must also survive focus churn and activation/
reactivation handshakes, during which the compositor may re-send key events that
were already in flight when the grab was rebuilt.

## Decision

The grab is managed by an explicit, observable state machine with safety
backstops.

- Per-key tracking with key *generations*: each grab incarnation has a
  generation so re-sent keys after a rebuild can be classified deterministically
  rather than double-processed.
- The grab is rebuilt via a hard reset on focus-in / reactivation. When an
  existing grab is replaced, `suppress_stale_keys` discards the compositor's
  re-sent presses; a fresh activation does not suppress (the re-sent key is the
  first the IME sees).
- Conservative forwarding: a key the IME does not consume is forwarded as its
  original press/release pair, and virtual-keyboard modifier state is kept in
  sync separately. Modifier changes must not synthesise releases for unrelated
  non-modifier keys.
- Two backstops against a stuck/locked keyboard: an emergency-exit shortcut that
  releases the grab and stops the daemon, and a guard failsafe that does the same
  after a streak of rejected presses while not in a transition phase.

The detailed maintenance rules live in `docs/dev/maintenance.md`; timing rules
live in `docs/explanation/timing-model.md`.

## Alternatives considered

- **Passive / no grab**: Rejected. Without the key stream the IME cannot compose
  or run candidate navigation.
- **Global key hook outside Wayland**: Rejected. Not Wayland-native and breaks
  the compositor's input routing and security model.
- **No failsafe**: Rejected. A bug in routing or lifecycle could leave the grab
  held with all keys rejected — a fully locked keyboard with no recovery.

## Consequences

- Positive: re-sent keys, modifier transitions, and focus churn are handled
  deterministically rather than heuristically.
- Positive: the user can always recover from a wedged grab.
- Trade-off: the state machine is intricate and changes must update the
  maintenance manual and timing model alongside the code.
- Negative (accepted): the failsafe can terminate the daemon — judged strictly
  better than an unrecoverable locked keyboard.
