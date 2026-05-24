# Wayland Input Method Protocol

Typio is a Wayland-native input method. Every key the user presses, every preedit string shown, and every candidate popup positioned travels through the `zwp_input_method_v2` family of unstable protocols. This document maps how Typio implements those protocols, what workarounds it applies to the unstable surface, and where the detailed rules live.

This is a **connective-tissue** document: it does not replace the protocol specification, the source-code comments, or the deep-dive timing model. It exists so a reader can answer "how does Typio handle X?" in one stop rather than grepping across `wl_input_method.c`, `wl_keyboard.c`, and five other explanation files.

For the protocol specification see the upstream [wayland-protocols `input-method-unstable-v2.xml`](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/unstable/input-method/input-method-unstable-v2.xml). For the happy-path typing pipeline see [Composition State Machine](composition-state-machine.md); for timing and lifecycle rules see [Timing Model](timing-model.md); for daemon resilience see [Lifecycle Resilience and Recovery](lifecycle-resilience.md).

> The lifecycle described here reflects [ADR-0011](../adr/0011-composition-and-lifecycle-rewrite.md): protocol events are recorded as **facts**, the session state is **derived** (`desired = reduce(facts)`), and effects are applied as an **idempotent diff**. There is no stored phase machine, no `pending_reactivation` flag, and no separate reconciler — those are folded into the per-step diff. (The code migration is tracked with ADR-0011.)

## Protocol Stack

Typio binds five Wayland protocol layers. The input-method layer is the one Typio *implements*; the others are dependencies or peers.

```mermaid
flowchart TD
    subgraph App["Client Application"]
        TI["zwp_text_input_v3"]
    end
    subgraph Comp["Compositor"]
        TIM["zwp_text_input_manager_v3"]
        IMM["zwp_input_method_manager_v2"]
        IGRAB["zwp_input_method_keyboard_grab_v2"]
        IPOP["zwp_input_popup_surface_v2"]
        WCOMP["wl_compositor"]
        WOUT["wl_output"]
    end
    subgraph Typio["Typio Daemon"]
        IM["zwp_input_method_v2"]
        KB["grab listener"]
        VK["zwp_virtual_keyboard_v1"]
        POP["popup wl_surface"]
    end

    TI -->|enable / commit| TIM
    TIM -->|activate / deactivate / done| IM
    IM -->|set_preedit_string / commit_string| IMM
    IMM -->|forward| TI
    IM -->|grab_keyboard| IGRAB
    IGRAB -->|key / modifiers| KB
    KB -->|unhandled keys| VK
    VK -->|key / modifiers| Comp
    IM -->|get_input_popup_surface| IPOP
    POP -->|present| IPOP
    Typio -->|create_surface| WCOMP
    WOUT -->|scale| POP
```

The protocol surface is split by who implements the interface.

### Compositor-provided interfaces (Typio is the consumer)

| Interface | How Typio uses it |
|---|---|
| `zwp_input_method_manager_v2` | `get_input_method()` → receives the `zwp_input_method_v2` object Typio listens on |
| `zwp_input_method_keyboard_grab_v2` | `grab_keyboard()` → receives raw key/modifier/keymap events for the focused input context |
| `zwp_input_popup_surface_v2` | `get_input_popup_surface()` → positions the candidate popup near the cursor |
| `zwp_text_input_manager_v3` | Typio does not bind this directly, but relies on the compositor exposing it so client applications can participate in the text-input session |
| `wl_compositor` | `create_surface()` → creates the popup `wl_surface` backed by a Vulkan swapchain |
| `wl_output` | Listens to `scale` events so the popup renders at the correct DPI for each monitor |

### Client-provided interfaces (Typio depends on their presence)

| Interface | Role |
|---|---|
| `zwp_text_input_v3` | The application side (Firefox, terminal, chat) enables text input, sends surrounding text and content type to the compositor, which forwards it to Typio through `zwp_input_method_v2` |

### Typio-provided interfaces

| Interface | Role |
|---|---|
| `zwp_input_method_v2` | Listens for `activate`, `deactivate`, `done`, `surrounding_text`, `text_change_cause`, `content_type`, `unavailable`; sends `set_preedit_string`, `commit_string`, `commit`, `delete_surrounding_text` |
| `zwp_virtual_keyboard_v1` | Forwards unhandled keys back to the compositor as synthetic press/release events; managed by the vk-bridge module for keymap handoff and readiness gating |

## Event Handlers: events become facts

Every `zwp_input_method_v2` event does one thing — **record a fact**. No handler mutates a phase, rebuilds a grab, or decides a transition; that all happens when the per-step diff runs (at `done` and once per event-loop iteration). This is what keeps the handlers trivial and the lifecycle in one place.

### `activate` / `deactivate`

Record the pending focus fact (`active = true` / `false`) for the current session, creating the session if none exists. They make no decision: an `activate` that arrives while already focused is just the same fact re-recorded — the next diff is a no-op until something actually changes, which is why "deferred reactivation" needs no flag.

### `surrounding_text`, `text_change_cause`, `content_type`

Record client editing-context facts (buffered during the `done` batch). These are hints, not commands; the engine may ignore them or use them to improve prediction.

### `done`

The compositor's double-buffer commit point, and where the per-step diff runs:

1. **Serial increment**. `im_serial++`. The serial is the count of `done` events received; it is the commit serial for every `zwp_input_method_v2_commit()` call.
2. **Apply facts**. The buffered `surrounding_text`, `content_type`, `text_change_cause`, and `active` facts become current atomically.
3. **Reduce + diff**. `desired = reduce(facts)`; `apply(diff(desired, observe(resources)))`. Focus gained, focus lost, a re-activate while focused, and a no-op all fall out of the *same* computation — there is no `was_active`/`now_active` branch tree and no separate stale-grab rescue. If `observe()` reports a grab that should not exist (or a missing one that should), the same diff corrects it here.

### `unavailable`

Another input method has taken the seat. Typio sets `frontend->running = false`, logs a warning, and stops.

## Commit Serial and the Chokepoint

`zwp_input_method_v2` requires a serial on every `commit()` call. The serial must match the most recent `done` event. Before the first `done`, the serial is 0.

Typio treats serial 0 as a **write barrier**: `typio_wl_commit()` refuses to send `set_preedit_string` or `commit_string` when `im_serial == 0`. This prevents a race where the IME stages preedit text before the compositor has established the input-method connection, which would cause the compositor to silently drop the staged text without error.

```c
if (frontend->im_serial == 0) {
    /* Skip: compositor has not sent done yet. */
    return;
}
zwp_input_method_v2_commit(im, frontend->im_serial);
frontend->last_committed_serial = frontend->im_serial;
```

`last_committed_serial` is a diagnostic breadcrumb. It is also the hook for future reconnect work: if the compositor restarts and the serial resets, Typio can detect the discontinuity.

This chokepoint is the single place where every protocol write is validated. All preedit updates, commit strings, and popup geometry requests funnel through `typio_wl_commit`.

## Keyboard Grab Lifecycle

The grab and its keymap handshake are **one resource** (`absent → needs_keymap → ready → broken`) that the diff creates and tears down; the timing rules are in [Timing Model](timing-model.md), and the rationale (now folded into the derived model) in [ADR-0011](../adr/0011-composition-and-lifecycle-rewrite.md).

Briefly:
- Each grab incarnation has an **epoch**. Every recorded key fact carries the epoch it arrived under, and a key whose epoch ≠ the current grab epoch is dropped at routing — this is the single fence that replaces the old generation arrays, `suppress_stale_keys`, and `created_at_epoch`.
- When a grab is rebuilt (focus-in, re-activate, resume, reconnect), the compositor may re-send already-in-flight keys; the epoch fence discards them.
- Unhandled keys are forwarded as original press/release pairs through the virtual keyboard.
- Modifier state is synced separately; modifier changes do not synthesise releases for unrelated non-modifier keys.
- Two fail-safe backstops remain: an emergency-exit shortcut and a rejected-press-streak threshold, both releasing the grab.

## Virtual Keyboard Forwarding (`vk_bridge.c`)

`zwp_virtual_keyboard_v1` is Typio's output path for keys the engine declined (`TYPIO_KEY_NOT_HANDLED`). The vk-bridge module manages:

- **Keymap handoff** — when the compositor delivers a new keymap on the grab, the vk must receive the same keymap before forwarding keys, or modifier mappings will mismatch.
- **Readiness gating** — vk forwarding is blocked until the keymap is confirmed, preventing modifier-sync errors during activation handshakes.
- **Fail-safe downgrade** — if vk health degrades (keymap deadline missed, compositor stalls), Typio falls back to local key handling rather than forwarding broken state.

## Re-activate while focused needs no special case

A subtle protocol behaviour: the compositor may send `activate` while Typio is still focused on the same surface (e.g. the user clicked from one text field to another inside the same window). Treating this as a full `deactivate` → `activate` cycle would tear down the grab, lose the preedit round-trip, and interrupt typing.

Under the derived model this requires **no detection and no flag**. `activate` just re-records `active = true` — a fact that is already true. At the next `done`, `desired = reduce(facts)` is unchanged, so `diff` is empty and the grab, serial, and composition are left untouched. The old `pending_reactivation` flag and the reactivation branch existed only to avoid the teardown that a hand-written transition tree would otherwise perform; with derived state there is nothing to avoid.

## Resume and silent grab loss

System suspend is invisible to the Wayland protocol: no `deactivate` before sleep, no guaranteed `activate` after wake; a held modifier may be stuck and the grab may be silently dead on wake. The compositor can also drop the grab with no event at all (restart, bug, race).

How much of this the per-step diff handles depends on whether `observe()` can *see* it — and `observe()` reads resource *presence*, not *liveness*:

- **Suspend.** A grab dead across suspend leaves a *live proxy*; `observe()` reports it healthy, so the diff alone is blind. A resume **detector** (logind `PrepareForSleep` plus a `CLOCK_BOOTTIME` vs `CLOCK_MONOTONIC` gap heuristic — see [Lifecycle Resilience](lifecycle-resilience.md)) records facts: it invalidates the grab epoch and drops the compositor-visible preedit, then lets the step run.
- **Grab object gone.** If the grab *object* is actually absent while `desired` wants it (typically right after one of our scrubs, or a connection drop surfacing as `POLLHUP`), `observe()` sees it and the next per-iteration `diff` rebuilds — no special routine.

In both cases the input context is never `focus_out`'d, so the engine's in-flight composition survives, and the rebuild is the *same* grab build used on first focus. There is no separate reconciler with a divergence timer — but note the limit: a grab the compositor silently orphans (proxy alive, no event, no suspend) is **not** auto-recovered ([Lifecycle Resilience §Known limits](lifecycle-resilience.md#known-limits)).

## Preedit Round-Trip Optimisation

When the user navigates candidates with `Up`/`Down`, only the `selected` index changes; the preedit text is identical. Typio detects this in `update_wayland_text_ui`:

```c
update_plan = typio_wl_text_ui_plan_update(session->last_preedit_text,
                                           session->last_preedit_cursor,
                                           new_text, cursor_pos);
```

If `update_plan == POPUP_ONLY`, the popup is repainted synchronously but the expensive `zwp_input_method_v2.set_preedit_string` → `done` round-trip to the application is skipped entirely. This avoids composition-update jank in heavyweight clients like Chrome.

## Session Checkpoint Persistence

On every real preedit delta, Typio saves an engine-specific checkpoint to `$XDG_RUNTIME_DIR` (tmpfs, so the atomic write is cheap). If the daemon crashes and systemd restarts it, the first `focus_in` attempts `checkpoint_try_restore`, allowing the user to resume the half-typed composition. See [Engine Contract](engine-contract.md) for the engine-side `snapshot_session` / `restore_session` contract.

## Source Map

| Protocol object | Source file | Responsibility |
|---|---|---|
| `zwp_input_method_v2` | `wl_input_method.c` | Event handlers (record facts), serial chokepoint |
| Session lifecycle | `session_controller.{c,h}` | `reduce(facts) → desired`; the per-step driver |
| Effects | `session_effects.{c,h}` | `observe(resources)`, `diff`, `apply` (grab build/teardown, recovery) |
| `zwp_input_method_keyboard_grab_v2` | `wl_keyboard.c` | Grab create/destroy, key/modifiers/repeat listeners, emergency exit |
| Key epoch + tracking | `key_tracker.{c,h}` | Epoch fence and symmetric press/release |
| `zwp_virtual_keyboard_v1` | `vk_bridge.c` | Keymap handoff, readiness gating, unhandled-key forwarding, fail-safe downgrade |
| `zwp_input_popup_surface_v2` | `candidate_popup.c` | Popup geometry, present, retry-on-stall |
| Popup rendering | `candidate_popup_paint.c` | Vulkan swapchain on `wl_surface` |
| Resume detection | `resume_signal.c` | logind + boottime heuristic (records facts) |
| Session checkpoint | `session_checkpoint.c` | Atomic save/restore to tmpfs |
| Protocol XML | `protocols/input-method-unstable-v2.xml` | Wayland protocol definition (upstream) |

## See Also

- [Architecture Overview](architecture-overview.md) — high-level component diagram and protocol stack
- [Timing Model](timing-model.md) — the derived reduce+diff state model, truth sources, event-loop scheduling
- [Lifecycle Resilience and Recovery](lifecycle-resilience.md) — suspend/resume, compositor restart, silent grab loss
- [ADR-0011: Composition and session-lifecycle rewrite](../adr/0011-composition-and-lifecycle-rewrite.md) — the derived model, epoch fence, atomic composition
- [ADR-0007: Wayland input method v2](../adr/0007-wayland-input-method-v2.md) — why Typio uses the unstable v2 protocol
- [Popup Appearance](../dev/popup-appearance.md) — Vulkan popup rendering pipeline
- [Composition State Machine](composition-state-machine.md) — abstract typing pipeline
