# Timing Model

## Purpose

This document defines the timing model for Typio's Wayland input-method path. It exists to keep event ordering, ownership, and cleanup rules explicit.

If a keyboard or focus bug appears "sometimes", treat it as a timing-model problem first, not as a one-off key handling bug.

> This model reflects [ADR-0011](../adr/0011-composition-and-lifecycle-rewrite.md): the session lifecycle is a **single derived state** (`desired = reduce(inputs)`) reconciled against live resources by an **idempotent diff**, not a stored phase machine. Recovery is a property of that diff, not a set of bespoke paths.

The most failure-sensitive chain is the **build-up order** that `apply()` must follow when bringing a focused session to a state where keys reach the engine and unhandled keys reach the app:

1. `zwp_input_method_v2` activation (a focus fact arrives)
2. keyboard-grab creation
3. compositor keymap delivery on the grab
4. keymap forwarding into `zwp_virtual_keyboard_v1`
5. virtual-keyboard transition to `ready`
6. only then: unhandled-key forwarding to the focused application

If that chain is incomplete or reordered, the frontend must not behave as if virtual-keyboard forwarding is healthy.

## The model: one derived state, reconciled by diff

The frontend stores only two kinds of thing:

- **input facts** — recorded verbatim as events arrive (see [Truth Sources](#truth-sources))
- **live resources** — the grab object, the vk keymap generation, the last composition sent on the wire

It stores **no** lifecycle phase. Every event and every event-loop iteration runs one step:

```
desired = reduce(facts)            pure: should we grab? is a preedit owed? which serial?
actual  = observe(resources)       a view of live fields, never a stored copy
effects = diff(desired, actual)    the minimal idempotent ops to make actual == desired
          apply(effects)           create/destroy grab, sync keymap, send/clear preedit, commit
```

`reduce` and `diff` are pure and unit-tested; `observe` and `apply` are the only parts that touch live Wayland state. Applying the same `desired` twice produces no effects — that idempotence is what makes recovery free.

### Derived states (names, not stored fields)

The old `inactive / activating / active / deactivating` enum is gone. Those names survive only as *descriptions* of a derived `(focus × grab-readiness)` pair, useful in logs and `RuntimeState`:

| Name | Derived condition | Keys to engine? |
|---|---|---|
| `inactive` | disconnected, or not focused, or no keyboard engine wanted | no |
| `activating` | focused and a grab is wanted, but the grab is not `ready` yet | no (modifiers only, see below) |
| `active` | focused and the grab resource is `ready` | yes |

There is no stored `deactivating`: losing focus simply changes `desired`, and the next `diff` tears the grab down. "Deferred reactivation" is likewise not a flag — a re-activate while focused just updates the facts; the next step re-derives `desired` and the diff is a no-op until something actually changed.

## Truth Sources

Each input fact has exactly one source. Facts are recorded, never interpreted at arrival:

- `activate / deactivate / done(serial)`: focus + the compositor double-buffer commit point
- `key press / release`: physical key truth (carries the current grab epoch)
- `modifiers`: modifier-mask truth
- `repeat_info / repeat timer`: repeat truth
- `surrounding_text / content_type`: client editing context
- suspend gap, connection up/down: environment truth
- virtual keyboard output: **side effect only, never a source of internal truth**

Do not derive lifecycle truth from forwarded virtual-keyboard output. Additionally:

- a live keyboard grab is not proof that the virtual keyboard is ready
- a previously healthy virtual keyboard is not proof that the current grab has a current keymap

`observe()` is a **view of reality, never a stored second source of truth**, so the derived state cannot drift from the resources it describes. This is the single rule that retired the old declared-vs-observed reconciler.

## Ownership

- `session_controller.{c,h}` owns `reduce(facts) → desired` (pure) and the per-step driver that records facts and runs the step
- `session_effects.{c,h}` owns `observe(resources)`, `diff(desired, actual) → effects`, and `apply` — including grab create/destroy and the teardown-to-clean-slate used by every recovery
- `key_tracker.{c,h}` owns the per-key epoch stamp and symmetric press/release tracking — mutable, and **never** the routing decision
- `key_route.{c,h}` owns the pure routing decision `(key, mods, state) → {action, reason}`
- `wl_keyboard.c` owns key-event interpretation (XKB → `TypioKeyEvent`) while the derived state is `active`
- `vk_bridge.*` owns virtual-keyboard health, keymap deadlines, readiness gating, and fail-safe downgrade
- `wl_event_loop.c` owns poll scheduling, bounded auxiliary-fd dispatch, and deadline-aware wakeups
- `wl_runtime_config.c` owns config-watch events, debounce timing, watch rearming, and the runtime reload boundary
- `voice_service.c` owns voice recording/inference state and deferred voice reload application
- `xkb_state` owns the logical modifier view
- engine implementations own only engine/composition behavior

The status D-Bus surface exports this state but does not own it. `RuntimeState` is a read-only projection of `observe()`, not an independent tracker.

## Grab + keymap: one resource, one readiness

The keyboard grab and its virtual-keyboard keymap handshake are **one resource** with a single readiness state. There is no separate phase plus vk state machine plus "non-routable grab" rescue branch:

- `absent`: no grab object exists
- `needs_keymap`: a grab exists, but the current grab epoch has not completed the keymap handoff
- `ready`: the current epoch delivered a compositor keymap to the virtual keyboard; key/repeat processing and unhandled-key forwarding may proceed
- `broken`: the path is unhealthy and must not be trusted; a fail-safe condition

Rules:

- creating/rebuilding the grab starts a new epoch and forces `needs_keymap`
- old `ready` must never survive into a new grab epoch
- `ready` requires a compositor keymap observed in the current epoch
- a timeout in `needs_keymap`, or any `broken`, is a fail-safe condition — prefer releasing the grab over forwarding through a partially broken path
- modifier-mask updates may apply while `needs_keymap` (the derived `activating` case) so held Ctrl/Alt/Super survive grab creation before the first new key press; key presses may not

## One epoch fence

"A key from before this grab is untrusted" is enforced **once**, by the grab epoch — not by three overlapping mechanisms. Every grab incarnation has an epoch; every recorded key fact carries the epoch under which it was received. At routing time a key whose epoch ≠ the current grab epoch is dropped (a compositor re-send of an already-held key across a rebuild, suspend, or reconnect). There is no separate `suppress_stale_keys` flag, per-key generation array, or `created_at_epoch` timestamp — one stamp, one comparison.

## Teardown is one operation

Every transition that ends a grab — focus loss, suspend, reconnect, fail-safe, or a derived-state change — runs the **same** teardown in `apply`:

- forwarded keys are released to the virtual keyboard
- virtual-keyboard modifiers are reset to zero (exception below)
- key repeat is cancelled
- the grab object is destroyed and a new epoch is begun
- per-key tracking is cleared
- any stale assumption that vk is `ready` is discarded; the next epoch must re-earn `ready`

The one exception is a focus handoff (the derived `activating`-from-focused case): the last compositor-reported modifier mask may be carried to the virtual keyboard so the newly focused client can still observe a held shortcut modifier. Carried modifier state must be cleared before the next grab is built. A suspend/reconnect teardown carries nothing — a modifier held across the boundary produced no key-up and is dropped unconditionally.

## Recovery is the same step

Recovery shares the normal path **only for divergences `observe()` can see** — `observe()` reads resource *presence*, not *liveness*, so the diff is a backstop for our own state, not a detector of external silent grab death:

- **Internal divergence** — the grab *object* is missing while `desired` wants it (typically right after one of our scrubs). `observe()` reports `grab = absent`, the next `diff` rebuilds. No divergence timer, because there is no stored phase to diverge. This is the free part.
- **Suspend/resume** — a grab dead across suspend leaves a *live proxy*, which `observe()` cannot distinguish from a healthy one. A resume detector therefore records the gap fact and invalidates the grab epoch; the next diff then rebuilds. The input context is never `focus_out`'d, so the engine's in-flight composition survives.
- **Compositor reconnect** — connection death surfaces as `POLLHUP`; it is recorded as a fact that makes `desired` inactive, and the fresh `activate` on reconnect drives the rebuild. Engine/session state, aux handlers, the config watch, and the resume detector are preserved.

A grab the compositor orphans with *no* protocol event, suspend, or disconnect is invisible to `observe()` and is **not** auto-recovered. See [Lifecycle Resilience §What the diff can and cannot see](lifecycle-resilience.md#what-the-diff-can--and-cannot--see) and its [Known limits](lifecycle-resilience.md#known-limits).

## Shortcut policy

Application shortcuts are decided in the Wayland frontend, as a pure routing decision:

- routing yields two independent dimensions: `action` (`consume` / `forward`) and `reason`; the per-key tracker records lifecycle history (forwarded, app-shortcut) for symmetric release, and is **not** the routing model
- non-modifier keys with Ctrl, Alt, or Super bypass the engine; the matching release must also bypass it
- Typio-reserved shortcuts (emergency exit, voice PTT) are consumed internally and never treated as virtual-keyboard forwarding
- emergency exit is the highest-priority reserved decision on key press: dump recent logs, release the grab, stop the frontend — it forwards no key
- engines do not each implement shortcut bypass; `Ctrl+Shift`-style modifier-only shortcuts stay transparent to the app/compositor

## Event Loop Scheduling

The frontend uses one poll loop for Wayland and auxiliary runtime sources. Auxiliary fds are part of the timing model because they can otherwise delay keymap deadlines, lifecycle cleanup, or user-visible config changes.

- the candidate popup is rendered once per loop iteration from a coalesced `popup_update_pending` flag, never inline in the composition callback
- the popup's GPU present runs on the loop thread and must stay bounded: `flux_surface_begin_frame` uses a finite timeout so a compositor that stops releasing swapchain images (display asleep / occluded after a lock or suspend) cannot block the loop. A timed-out present skips the frame and re-arms `popup_update_pending`; repeated stalls recreate the swapchain. See [ADR-0006](../adr/0006-resilient-candidate-popup-present.md)
- while the grab resource is `needs_keymap`, the poll timeout must not sleep past the current keymap deadline
- status and tray D-Bus dispatch are bounded per tick so a busy bus cannot starve Wayland dispatch, voice completion, repeat, or config reload
- config watch events schedule a debounced reload instead of reloading per inotify event; watches are rearmed after the watched file is deleted, moved, or replaced by an editor save
- voice reload is deferred while recording/inference owns the engine snapshot, then applied once the job completes; the voice fd is refreshed when runtime config changes

## Invariants

- the lifecycle state is **derived every step**, never stored or hand-mutated
- `apply` effects are idempotent: re-applying the same `desired` is a no-op
- no key press/release is processed unless the grab resource is `ready`
- modifier-mask updates may be processed while `needs_keymap` to resynchronize held modifiers
- no virtual-keyboard forwarding happens unless vk is explicitly `ready`
- a key whose epoch ≠ the current grab epoch is dropped at routing
- no per-key tracking state survives a teardown
- application shortcut press/release stays symmetric
- a rebuilt grab never inherits prior-epoch keymap health
- fail-safe paths prefer releasing the grab over running partially broken
- config reload bursts coalesce into a single runtime reload once the filesystem settles
- an engine-switch failure must not silently clear the previously active engine in that category

## Observability Contract

Logs and `RuntimeState` serve different purposes and stay layered:

- `RuntimeState` is the authoritative live snapshot — a projection of `observe()`
- logs are the ordered event history explaining how the frontend reached that state
- trace topics are a `debug` surface, not a second state model

Responsibility split:

- derived-state-edge summaries belong to `session_controller.{c,h}`
- teardown-cause and grab create/destroy logs belong to `session_effects.{c,h}`
- virtual-keyboard health and fail-safe logs belong to `vk_bridge.*`
- per-key sequencing and modifier-path traces belong to `wl_keyboard.c`
- watchdog and dispatch-path logs belong to `wl_event_loop.c`

Do not duplicate one transition across layers at the same log level. Prefer `debug` detail in a helper and one `info` summary at the boundary owner.

### Runtime fields for timing diagnosis

`RuntimeState` exports the projection of `observe()`. The highest-value fields:

- `derived_state` (`inactive` / `activating` / `active`)
- `grab_state` (`absent` / `needs_keymap` / `ready` / `broken`)
- `grab_epoch`
- `keyboard_grab_active`
- `virtual_keyboard_state`, `virtual_keyboard_has_keymap`, `virtual_keyboard_keymap_generation`
- `virtual_keyboard_drop_count`, `virtual_keyboard_state_age_ms`
- `virtual_keyboard_keymap_deadline_remaining_ms`

A healthy active session: `derived_state=active`, `grab_state=ready`, `keyboard_grab_active=true`, `virtual_keyboard_state=ready`, `drop_count` stable. `grab_state=needs_keymap` while focused for longer than the keymap deadline is the primary clue that the grab→keymap→vk chain did not close.

## Log Level Policy

- `debug` — per-event sequencing, repeated grab/keymap churn, routing internals, trace-topic output
- `info` — low-frequency, user-relevant boundaries: focus changes, grab create/destroy summaries, vk epoch transitions, recovery to `ready`
- `warning` — recoverable anomalies: repeated grab rebuilds, repeated keymap cancellation before readiness, growing drop counts, fallback paths
- `error` — fail-safe entry, timeout shutdown, broken invariants, display/protocol failures that stop forwarding

Operational rules:

- a high-frequency path should not emit one `info` per event
- repeated anomalies prefer one aggregated `warning` plus `debug` detail
- `info` answers "what durable boundary did the frontend just cross?"; `debug` answers "why, and in what sequence?"

## Trace Capture

For shortcut-routing or repeat bugs:

```sh
typio-daemon --verbose 2>&1 | tee typio-trace.log
```

Read traces in this order: sort by `seq`, group by `topic`, compare `grab_state`, `grab_epoch`, `mods`, `phys`, and `xkb`. For `Ctrl-T`-style bugs, inspect `TRACE key`, `TRACE vk_key`, and `TRACE vk_modifiers`. A release whose `epoch != grab_epoch` is a cross-boundary orphan and is expected to be dropped at routing.

## Test Expectations

Timing-model regressions should be covered by:

- `reduce` tests: facts → `desired` across focus/engine/connection combinations
- `diff` tests: every `(desired, actual)` pair yields the minimal idempotent effect set; re-applying is a no-op
- `key_tracker` tests: epoch fencing and symmetric press/release across teardown
- routing tests: pure `(key, mods, state)` decisions including reserved shortcuts
- vk state-machine tests: `needs_keymap` / `ready` / `broken` / keymap-timeout transitions
- recovery property tests: a persistent grab-loss always converges within one step; suspend/reconnect re-derive the same `desired`

Every guard deleted from the old model (startup suppression, boundary carry, divergence repair) must first be re-expressed as a failing `reduce`/`diff` test before its imperative code is removed.
