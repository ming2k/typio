# Lifecycle Resilience and Recovery

How the Wayland frontend survives events that no compositor protocol message announces cleanly — system suspend/resume, compositor restarts, and silent grab loss. This is the operational counterpart to the [Composition Lifecycle](composition-lifecycle.md) (which describes the happy-path pipeline) and the [Timing Model](timing-model.md) (which describes the phase machine).

## The problem class

The input-method lifecycle is driven by `zwp_input_method_v2` events: `activate`, `deactivate`, `done`. That works as long as the compositor narrates every state change. It does not narrate:

- **System suspend.** While the machine sleeps the kernel delivers no key-up for a held key. On wake, a modifier or repeating key can be stuck, and the compositor may or may not re-run the IM handshake.
- **Compositor restart / session re-init.** Our keyboard grab can vanish without a matching `deactivate`, leaving us convinced we are `ACTIVE` while reality has no grab — and no event will ever correct us.
- **Lost protocol messages.** A dropped `done` leaves the believed phase and the real state out of sync.

The shared shape: *the event that would have triggered recovery never arrives.* Event-driven code alone cannot solve this. Two mechanisms close the gap.

## Mechanism 1 — Resume signal (Stage 1, shipped)

`resume_signal.{c,h}` detects "the kernel just resumed us" through two complementary detectors that converge on one callback:

1. **logind `PrepareForSleep`** — a system-bus signal emitted around every suspend/hibernate. Reliable where present; absent on non-systemd setups and minimal containers. Built only when libdbus is available (`HAVE_STATUS_BUS`), on a private system-bus connection with `exit_on_disconnect` disabled so logind churn never takes the IME down.
2. **Boottime/monotonic gap heuristic** — `CLOCK_BOOTTIME` advances during suspend, `CLOCK_MONOTONIC` does not (`monotonic_time.h`). The event loop ticks `typio_wl_resume_signal_tick` once per iteration; a boot-minus-monotonic gap above `TYPIO_WL_RESUME_GAP_THRESHOLD_MS` (2 s) means we slept. Always built, so it covers compositors where logind is missing or its signal is lost.

Both fire `resume_signal_fire`, de-duplicated by a 5 s monotonic cooldown so a coincident logind notice and detected gap recover only once. The pure decision logic (`typio_wl_resume_gap_exceeded`, `typio_wl_resume_in_cooldown`) lives in `resume_model.h` and is unit-tested in `test_resume_model.c`.

The callback runs `typio_wl_input_method_handle_resume`, which:

- captures whether we were actively composing in a focused client *before* scrubbing;
- calls `typio_wl_lifecycle_on_resume` — the scrub: tear down the grab, drop stale/carried modifiers unconditionally (a modifier held across suspend never produced a key-up), bump `active_key_generation` to fence out any stale key the compositor re-delivers, clear the per-key tracking and generation arrays, drop the compositor-visible preedit, and force the phase to `INACTIVE`;
- if we *were* composing, proactively rebuilds the grab rather than waiting for an `activate` that may never come. The input context is never `focus_out`'d, so the engine's in-flight composition survives the wake.

## Mechanism 2 — Reconciler (Stage 2, shipped)

The resume signal handles the cases it can detect. The reconciler is the backstop for everything else: it makes the lifecycle *self-correcting* instead of purely event-driven.

### Orthogonal state model

The legacy `TypioWlLifecyclePhase` is a single enum that conflates four independent concerns. `lifecycle_state.{c,h}` decomposes the real state into orthogonal axes:

| Axis | States | Observed from |
|---|---|---|
| `conn` | DISCONNECTED / CONNECTED | `frontend->display` |
| `focus` | UNFOCUSED / FOCUSED | input context focus |
| `grab` | NONE / PENDING_KEYMAP / READY | `frontend->keyboard` presence |
| `comp` | IDLE / COMPOSING | last preedit text |

`typio_wl_lifecycle_observe` reads these from live frontend fields — it is a **view of reality, never a stored second source of truth**, so the two cannot drift. `typio_wl_lifecycle_project_phase` projects the axes onto a steady-state legacy phase, and `typio_wl_lifecycle_state_agrees` compares that projection against the declared phase. Transient phases (`ACTIVATING`/`DEACTIVATING`) are mid-handshake and deliberately never flagged. Pure; tested in `test_lifecycle_state.c`.

> Note: the grab axis means "keyboard grab exists" (the engine can receive keys), *not* virtual-keyboard keymap readiness. A vk-degraded-but-functional grab still routes keys and must not be torn down; vk health is tracked separately by `typio_wl_vk_health_check`.

### Reconcile loop

`reconciler.{c,h}` runs `typio_wl_reconcile_tick` once per event-loop iteration: observe, project, compare. The pure decision (`reconcile_model.c`, tested in `test_reconcile_model.c`) implements a debounced state machine:

```
agree                       -> OK     (clear timer)
diverge, none tracked       -> ARM    (start timer)
diverge, within threshold   -> WAIT
diverge, threshold exceeded -> REPAIR (clear timer, act)
```

A divergence must persist past `TYPIO_WL_RECONCILE_THRESHOLD_MS` (2 s) before any repair fires, so normal handshakes are never disturbed and a clock regression can't trigger a false repair. On `REPAIR` the reconciler reuses the resume recovery path (scrub + conditional regrab) rather than duplicating teardown logic. This is what catches the "compositor silently dropped our grab" wedge that no event would ever report.

## Protocol-write safety (Stage 2, shipped)

`typio_wl_commit` is the single chokepoint for every commit. It refuses to commit when the IM serial is still 0 — i.e. before the first `done`, when the input method is not yet established and the compositor would silently drop the staged preedit/commit_string. `last_committed_serial` records the last flushed serial as a diagnostic breadcrumb and a hook for the reconnect work below.

## Stage 3 — Durability and reconnect

The mechanisms above keep the *running* daemon converged on reality. Stage 3
extends resilience across a full process restart and a lost compositor
connection. All three parts have shipped.

### 3a. Engine session checkpoint (shipped)

Previously the Rime/engine session lived only in daemon memory, so a daemon
crash + systemd restart (`data/typio.service`, `Restart=on-failure`) lost a
half-typed composition. It now survives:

- Two **optional** function pointers on `TypioKeyboardEngineOps` (engine.h):
  `snapshot_session(engine, ctx, char **out, size_t *out_size)` and
  `restore_session(engine, ctx, const char *data, size)`. The engine owns
  the blob (caller frees). NULL entries mean "no durable session" — the
  built-in `basic` engine leaves them NULL; only stateful engines implement
  them, so the feature is never dead weight. The Rust core exposes
  `typio_engine_has_session_ops` / `typio_engine_{snapshot,restore}_session`
  dispatch wrappers (engine.rs), mirroring the existing process_key dispatch.
- **Wire format** (`checkpoint_codec.{c,h}`): a versioned, length-prefixed,
  little-endian, binary-safe blob — magic+version (reject incompatible
  builds), engine name (only restore into the same engine), optional client
  identity, and a `CLOCK_BOOTTIME` stamp. The boottime stamp does double
  duty: a stamp near the current boottime is fresh, while one larger than
  the current boottime is from a previous boot and is rejected (a reboot
  means the composition is gone). Pure encode/decode/validate, fuzzed for
  truncation and oversized length prefixes in `test_checkpoint_codec.c`.
- **Frontend glue** (`session_checkpoint.{c,h}`): atomic write
  (temp + `rename`) to `$XDG_RUNTIME_DIR/typio/session.ckpt` on every real
  preedit delta — `$XDG_RUNTIME_DIR` is tmpfs, so per-delta writes are cheap
  and lose nothing. The checkpoint is discarded on commit and on a cleared
  preedit. Restore is one-shot: attempted on the first activation after
  daemon start, then the file is unlinked so live-session checkpoints are
  never replayed on ordinary focus changes (where the engine session is
  still in memory).
- **librime implementation** (`rime_checkpoint.c`): snapshots the raw input
  buffer (`get_input`) plus `ascii_mode`; restore replays it through the
  current schema (`set_input`) and re-emits preedit/candidates. Both ops are
  gated by `RIME_API_AVAILABLE`, so an older loaded librime degrades to
  persisting/restoring nothing rather than calling past the RimeApi struct.

### 3b. In-process Wayland reconnect (shipped)

Previously `POLLHUP`/`POLLERR` (or a failed read/flush) on the display fd was
fatal and recovery was delegated to a systemd restart, losing all in-memory
state. The display-loss paths in `wl_event_loop.c` now route to
`typio_wl_frontend_reconnect` (`wl_frontend.c`) instead of exiting:

- The Wayland-object setup/teardown was factored into reusable
  `frontend_wayland_bind` / `frontend_wayland_unbind`, used by startup,
  shutdown, and reconnect. Reconnect unbinds, then re-`wl_display_connect`s
  with capped exponential backoff (`reconnect_backoff.{c,h}`: 250 ms → 8 s,
  12 attempts), rebinds globals, and recreates the input method, virtual
  keyboard, and text UI surfaces. Engine/session state, aux handlers, the
  config watch, and the resume signal are preserved, so an in-flight
  composition survives a compositor restart.
- On reconnect the lifecycle is reset to `INACTIVE`, the key generation is
  bumped and tracking cleared (a key held across the outage produced no
  key-up), and the watchdog is parked during the blocking backoff. The
  compositor's fresh activate handshake — backstopped by the reconciler —
  rebuilds the grab. The serial chokepoint and generation epoch make
  committing against the new connection safe.
- If every attempt fails the daemon exits and hands off to the service
  manager, rather than spinning forever. The backoff schedule is pure and
  unit-tested (`test_reconnect_backoff.c`).

### 3c. Property tests (shipped)

`test_state_machine_properties.c` drives long LCG-randomized input sequences
through the pure decision functions and checks each against an independent
re-derivation of its spec, with fixed seeds for reproducibility:

- `reconcile_model`: action/timer match the debounced rule over random
  agree/disagree sequences; a persistent divergence always REPAIRs within one
  threshold window (never permanently stuck); the timer resets on OK/REPAIR.
- `resume_model`: gap detection and cooldown match spec across random
  deltas/thresholds; a cooldown-gated detector never fires twice within a
  window; a fire always implies a strictly positive gap.
- `lifecycle_state`: exhaustive over all 24 axis combinations × 4 declared
  phases — projection matches spec, transient phases never count as
  divergence, the projection is never itself transient.
- `reconnect_backoff` and `checkpoint_codec`: randomized monotonicity/bounds
  and binary-safe encode→decode round-trips.

## See Also

- [Composition Lifecycle](composition-lifecycle.md) — the happy-path key-to-commit pipeline
- [Timing Model](timing-model.md) — activation phases and the virtual-keyboard state machine
- [Architecture Overview](architecture-overview.md) — components and the event loop
