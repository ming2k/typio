# ADR-0007: Wayland Input Method v2

- **Status**: Accepted
- **Date**: 2026-05-24
- **Deciders**: Project maintainers

> The decision to adopt `zwp_input_method_v2` stands. The *lifecycle mechanism*
> described in points 2 and 4 below (a stored phase machine validated by a separate
> reconciler over orthogonal axes) is superseded by
> [ADR-0011](0011-composition-and-lifecycle-rewrite.md): the session state is now
> **derived** every step and reconciled by an idempotent diff, with no stored phase
> and no standalone reconciler. The defensive serial handling (point 3) and
> protocol isolation (point 5) are unchanged.

## Context

Typio is a Wayland-native input method. To receive activation events, keyboard input, and popup placement, it must bind to a Wayland input-method protocol. At the time of this decision (early 2024), the only broadly supported protocol for this purpose was `zwp_input_method_manager_v2` / `zwp_input_method_v2`, defined in `input-method-unstable-v2.xml`.

The protocol is explicitly **unstable**. This carries risks: compositor behaviour varies, event ordering is not fully specified, and the protocol may be superseded by a future stable version. We needed to decide whether to adopt v2, wait for a hypothetical v3, or use a non-Wayland fallback.

## Decision

Typio implements `zwp_input_method_v2` directly and treats the unstable status as a risk to be managed, not a blocker.

The implementation strategy:

1. **Bind the protocol at runtime** via `zwp_input_method_manager_v2.get_input_method`. If the compositor does not advertise the manager, Typio exits cleanly with a descriptive error.
2. **Never assume compositor behaviour is uniform**. The event order across `activate`, `deactivate`, `done`, and grab creation varies between compositors. All state transitions go through a single phase machine (`lifecycle.c`) validated by a reconciler (`reconciler.c`).
3. **Defensive serial handling**. The `done` serial is a commit serial, not a sequence number. A serial of 0 means no `done` has arrived yet; Typio refuses to commit preedit or text in that state to avoid silently dropped writes.
4. **Self-correcting state**. Because the protocol does not announce grab loss or compositor restart, Typio observes four orthogonal axes (`conn`, `focus`, `grab`, `comp`) and repairs divergence autonomously.
5. **Isolate protocol knowledge in one layer**. `wl_input_method.c` is the only file that speaks `zwp_input_method_v2`. The engine layer knows nothing about Wayland. This limits the blast radius if the protocol changes.

## Alternatives considered

- **Wait for a stable v3**: Rejected. No stable version existed at project start, and no implementation timeline was available from the Wayland community. Waiting would have left Typio without a viable platform.
- **Use a D-Bus or portal fallback (ibus-style)**: Rejected. Typio is explicitly Wayland-native. A portal or D-Bus path would add a second platform abstraction, break compositor-integrated popup placement, and require maintaining two input paths.
- **Implement only `zwp_text_input_v3` and rely on the compositor to forward keys**: Rejected. `zwp_text_input_v3` is the *client* side. Typio is the input method, not the text client. It must bind the input-method side to receive activation and the keyboard grab.

## Consequences

- **Positive**: Typio works today on every compositor that supports `input-method-unstable-v2` (Sway, Hyprland, River, KWin, Mutter, and others).
- **Positive**: The unstable status forced a defensive design — reconciler, resume recovery, serial chokepoint — that improves robustness even on compositors with buggy v2 implementations.
- **Trade-off**: Some compositors send `activate` without a matching `deactivate`, or deliver keys before the first `done`. The phase machine and reconciler add complexity to handle these variations.
- **Negative (accepted)**: If a future stable protocol (v3) appears, Typio will need a new frontend module or a protocol-translation shim. The isolated-layer design (one file per protocol) makes this possible without touching the engine or core.
