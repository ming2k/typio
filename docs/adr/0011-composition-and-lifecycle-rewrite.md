# ADR-0011: Composition and session-lifecycle greenfield rewrite

- **Status**: Accepted
- **Date**: 2026-05-25
- **Deciders**: Project maintainers
- **Relationship**: Refines and partly supersedes the *implementation* described in
  [ADR-0003 (Keyboard grab lifecycle)](0003-keyboard-grab-lifecycle.md) and
  [ADR-0007 (Wayland input method v2)](0007-wayland-input-method-v2.md). The
  protocol *choice* (`zwp_input_method_v2` + `zwp_virtual_keyboard_v1`) is
  unchanged; the state model around it is replaced. Backward compatibility of the
  internal C ABI is **explicitly dropped** — there are no external ABI consumers
  that must be preserved.

## Context

The composition *theory* in
[Composition State Machine](../explanation/composition-state-machine.md) is sound
and is kept: `Idle/Composing`, the four `process_key` outcomes
(`NOT_HANDLED / HANDLED / COMPOSING / COMMITTED`), and the rule that the engine is
the sole author of composition state. The problem is everything *around* it — the
Wayland session lifecycle and the way composition state crosses the FFI boundary.
Both have been patched per-bug until the concepts no longer compose.

### Root cause: two lifecycle models plus a reconciler to keep them agreeing

| Model | Where | Role | Defect |
|---|---|---|---|
| Linear phase FSM `INACTIVE→ACTIVATING→ACTIVE→DEACTIVATING` | `lifecycle_model.c`, `frontend->lifecycle_phase` | **Declared** state, hand-mutated throughout `wl_input_method.c` | A *stored* second source of truth that drifts from reality |
| Orthogonal axes `conn × focus × grab × comp` | `lifecycle_state.c` | **Observed** state, a pure view of live fields | Only consumed by the reconciler; never became the driver. `comp` is dead in `project_phase`. |
| Reconciler (2 s debounce) + resume signal | `reconciler.c`, `resume_signal.c` | Detect declared-vs-observed drift and repair it | Exist *only because* the declared model can drift |

`lifecycle-resilience.md` already states the correct principle — *"observe is a
view of reality, never a stored second source of truth"* — but the migration
stopped halfway: the phase is still the driver, the axes are a passenger, and a
debounced reconciler papers over the inevitable divergence.

### Debt that grew from that root cause

- **`im_handle_done` is a 4-way imperative tree** (`transition_to_active` /
  `handle_reactivation` / `transition_to_inactive` / stale-grab rescue) driven by
  `was_active`, `now_active`, `pending_reactivation`, plus a separate
  "non-routable grab" recovery. This is the Wayland double-buffer
  (`pending`/`current`, `done` = commit) modeled ad hoc with scattered booleans.
- **Recovery is duplicated by boundary.** suspend, reconcile-repair, and reconnect
  all do "scrub + conditional regrab," with the scrub itself smeared across
  `lifecycle.c`, `key_tracking.c`, `vk_bridge.c`. There is no single
  *teardown→clean-slate* / *build→active* pair.
- **One concept, three fences.** "A key from before this grab is untrusted" is
  implemented three times: `active_key_generation` + `key_generations[]`;
  `suppress_stale_keys` + `startup_suppressed_count`; `created_at_epoch`.
- **`*_guard` / `*_bridge` are not domain concepts.** `boundary_bridge`,
  `candidate_guard`, `repeat_guard`, `startup_guard`, `key_arbiter` name "where the
  fix went," not a part of the model.
- **A coupling leak in the otherwise-clean composition layer.**
  `on_preedit_callback` is a deliberate no-op because rendering happens in
  `on_candidate_callback`. An engine that emits `set_preedit` without
  `set_candidates` silently never renders. The two callbacks pretend to be
  independent but are secretly ordered.

## Decision

Two layers are rewritten greenfield. Layer B (Wayland session lifecycle) adopts a
single derived state with diff-applied effects. Layer A (core composition ABI)
collapses the three independent emit calls/callbacks into one atomic composition
update. The composition *theory* and the core↔apps dependency direction are
unchanged.

### Layer B — session controller: one derived state, reduce + diff

There is no stored `lifecycle_phase`. The only persisted things are *raw input
facts* and *live resource handles*. Every event and every loop tick runs one step:

```
inputs  : recorded facts        (IM activate/deactivate/done(serial), focus,
                                  surrounding text, content-type, engine-present,
                                  suspend gap, connection up/down)
desired = reduce(inputs)         pure: grab wanted? preedit-on-wire? which serial?
actual  = observe(resources)     live: grab object? keymap synced? last preedit sent?
effects = diff(desired, actual)  minimal, idempotent
          apply(effects)         create/destroy grab, sync keymap, send/clear
                                  preedit, commit
```

The reconciler is promoted from a 2 s *backstop* to *the only mechanism*: the
normal event path and the recovery path are the same `reduce → diff → apply`.
Consequently **suspend, resume, reconnect, and silent grab-loss stop being special
cases** — each is just "an input changed or a resource vanished → recompute →
apply." There is no privileged happy-path to fall off of, so there is no recovery
tree, no divergence detector, and no debounce.

### Layer A — composition is *state*, commit is an *event*: two channels

The defect is not "three callbacks" but a wrong factoring. Preedit and candidates
are the **same concept** — the in-flight composition — and were split into two
state channels that could disagree and had a hidden render order. Commit is a
**different kind**: a one-shot event, not re-readable state. The correct long-term
split follows that ontology, not callback count:

- **Composition (state).** Preedit + candidates fuse into one value, emitted by one
  transactional call. The context is never half-updated. An *empty* composition
  (`segment_count == 0 && candidate_count == 0`) is the `Idle` state; there is no
  separate "clear" call and no `kind` enum — emptiness is the cleared state.
- **Commit (event).** Stays a distinct one-shot. It is deliberately **not** a
  variant of the composition struct, because a single `process_key` can both commit
  finalized text *and* leave a residual composition (e.g. Rime commits a completed
  phrase while keeping trailing pinyin in preedit). A terminal `COMMIT` variant
  cannot represent "committed X, now composing Y"; two channels can.

```c
/* State: replaces set_preedit + set_candidates and their two callbacks. */
typedef struct TypioComposition {
    size_t struct_size;          /* sizeof at author's compile time; append-only ABI */

    /* preedit */
    const TypioPreeditSegment *segments;
    size_t segment_count;
    int cursor_pos;              /* in Unicode scalar values, not bytes/graphemes */

    /* candidates */
    const TypioCandidate *candidates;
    size_t candidate_count;
    int page, page_size, total, selected;
    bool has_prev, has_next;
    uint64_t content_signature;  /* stable; excludes `selected` (selection-only delta) */
    uint64_t revision;           /* monotonic per ctx; cheap "anything changed?" key */
} TypioComposition;

void typio_input_context_set_composition(TypioInputContext *ctx,
                                         const TypioComposition *comp);

typedef void (*TypioCompositionCallback)(TypioInputContext *ctx,
                                         const TypioComposition *comp, void *ud);
/* Commit unchanged in shape, kept as its own ordered event channel: */
typedef void (*TypioCommitCallback)(TypioInputContext *ctx,
                                    const char *text, void *ud);
```

**Ordering contract.** Within one `process_key` turn the engine may emit any
sequence of composition updates and commits. The frontend must preserve commit
order *relative to* composition updates, but may coalesce consecutive composition
updates (last wins) and renders once per loop iteration. So "commit then compose"
becomes `commit_string("你好")` followed by a preedit write; "compose then commit"
clears preedit after the commit — both fall out of the same rule with no special
case. This removes the secret preedit↔candidate ordering, the no-op preedit
callback, and the per-emission render risk. The `content_signature` / `selected`
split that lets the popup classify selection-only deltas is preserved.

### Invariants (the constraints this ADR commits to)

1. **Lifecycle state is derived, never stored.** Only input facts and live
   resource handles persist. No code may hand-mutate a phase.
2. **All effects are idempotent and applied as a diff** against observed resources.
   Applying the same `desired` twice is a no-op — this is what makes recovery free
   rather than a separate path.
3. **Composition is emitted as one transactional value** (preedit+candidates
   together); the context is never half-updated. Commit is a separate ordered
   event. The frontend coalesces composition updates once per loop iteration and
   diffs against last-sent before any protocol write or repaint.
4. **One epoch fence.** Every key carries the current grab epoch; only
   current-epoch keys are trusted. `suppress_stale_keys`, the generation arrays,
   and `created_at_epoch` collapse into one stamp-and-compare.
5. **Grab + vk-keymap is one resource with one readiness state** — not a phase plus
   a separate vk state machine plus a "non-routable grab" rescue branch. Keys route
   to the engine only when that resource is `Ready`.
6. **Routing is pure `(key, mods, state) → decision`; the per-key tracker is the
   only mutable part**, and exists solely for symmetric press/release. Routing and
   tracking never share a struct.

### Known limitation: the diff sees only what `observe()` sees

The idempotent diff converges the running daemon on reality, but only for divergences `observe()` can detect — and `observe()` reads resource *presence*, not *liveness*. A resource that is **dead but still present** (canonically, a grab whose compositor-side routing stopped while the client proxy survives) projects as healthy, so the diff is blind to it. Such cases need an external **fact source** (the resume detector, a `POLLHUP`-driven reconnect) or a future **liveness probe** that turns the condition into a fact feeding `reduce`. This is a deliberate boundary, not an oversight: the diff is the backstop for *our own* state, not a detector of *external* silent loss. The code phase must not assume the diff alone recovers a silently orphaned grab. See [Lifecycle Resilience](../explanation/lifecycle-resilience.md#what-the-diff-can--and-cannot--see).

### ABI longevity rules

Dropping *backward* compat is not the same as forgoing *forward* evolvability.
Greenfield is the one chance to build the evolution hooks in, so the next change is
additive rather than another break. Engines are runtime-loaded `.so`s that author
composition across a version-skewed boundary, so these are load-bearing:

- **Append-only structs with a leading `size_t struct_size`.** Readers honor only
  the fields the writer's size covers; new fields are appended, never reordered or
  resized. This reuses the existing `TYPIO_ENGINE_INFO_SIZE` sentinel convention so
  the whole ABI evolves one way.
- **Uniform borrowed-pointer ownership.** Every pointer in `TypioComposition`
  (and the commit string) is borrowed and valid **only for the call/callback
  duration**; the receiver copies what it must retain. One rule for all fields kills
  the recurring use-after-free patch class — no per-field lifetime reasoning.
- **Pinned encoding contract.** All strings are UTF-8, NUL-terminated, never `NULL`
  for required fields (use `""`). `cursor_pos`, `selected`, and offsets count
  Unicode scalar values — not bytes, not grapheme clusters. Pinned once here so
  cross-implementation cursor bugs cannot recur.
- **Transactional emit only.** There is no incremental mutation API (no separate
  `set_preedit` / `set_candidates`). The engine builds a full composition and emits
  it in one call; partial states are unrepresentable.
- **Observability is a projection, never a tracker.** The `RuntimeState` D-Bus
  snapshot serializes `observe(resources)` directly. It must not maintain a second
  copy of lifecycle state (timing-model already forbids "trace as a second state
  model"); with derived state there is exactly one thing to read.

### Boundary placement (deliberate)

`reduce` and `diff` are pure but live in `daemon/` (C), **not** core. Per the
[core↔apps boundary rule](../explanation/architecture-overview.md#core--apps-boundary)
— "purity alone is not a reason to migrate to core; keep platform-named state
machines with their platform" — the lifecycle reduces over Wayland facts and is
Wayland-only, so it stays in the daemon. It is pure and property-tested *there*.
Composition state (Layer A) stays in core, because it is platform-agnostic and any
future host (X11, CLI, fuzzer) needs the same value.

### Module shape (target)

The `*_guard` / `*_bridge` / `reconcil*` / `lifecycle_state` cluster collapses into:

- `session_controller.{c,h}` — `reduce(inputs) → desired` (pure) + the per-step driver
- `session_effects.{c,h}` — `observe(resources)` + `diff(desired, actual) → effects` + `apply`
- `key_tracker.{c,h}` — epoch stamp + symmetric-release tracking (mutable, no routing)
- `key_route.{c,h}` — pure routing decision, kept (already pure-ish; tighten signature)

### What gets deleted

`lifecycle_phase` enum + validator · `pending_reactivation` ·
`pending.active`/`was_active`/`now_active` branching · the `im_handle_done` 4-way
tree · `reconciler.c`/`reconcile_model.c` as a standalone backstop (folds into the
step) · the divergence/threshold debounce · `lifecycle_state.{c,h}` projection ·
`boundary_bridge` · `startup_guard` · `candidate_guard` · `repeat_guard` · the
duplicate epoch fences · the no-op preedit callback path · the
`set_preedit`/`set_candidates` split and its two callbacks (the commit channel is
kept, now ordered against composition). `set_candidate_selection` folds into a
normal composition update (only `selected`/`revision` change).

## Alternatives considered

- **Finish the half-migration (make axes the driver, keep the phase enum as a
  cache):** Rejected. Keeping any stored projection of the lifecycle reintroduces
  exactly the drift the reconciler was built to chase. Derive, don't cache.
- **Keep three composition callbacks, just document the ordering:** Rejected.
  A required ordering between "independent" callbacks is a latent bug surface; the
  atomic update makes the ordering unrepresentable.
- **Incremental refactor in place:** Rejected by the project owner — the concepts
  are too entangled to refactor without an intermediate state that is worse than
  both endpoints. Greenfield with no backward-compat is the chosen path.

## Consequences

- Positive: suspend / resume / reconnect / silent grab-loss share the *one* code
  path; recovery is a property of idempotent diffing, not bespoke trees.
- Positive: the `done` decision, the phase validator, the reconciler debounce, and
  three fences all disappear — large net deletion of the most bug-dense code.
- Positive: composition can no longer half-render; the engine→frontend contract is
  one atomic value matching the documented state machine.
- Positive: the new structs carry a `struct_size` guard and an append-only rule, so
  this is the *last* ABI break — future composition fields are additive.
- Trade-off: the internal C ABI breaks once now (composition callback + emit
  function; commit channel keeps its shape). Engines (`basic`, `librime`) and the
  daemon frontend must be ported together in one change. Acceptable: there are no
  external ABI consumers.
- Trade-off: `reduce`/`diff` must be exhaustively property-tested to match the
  retired imperative behavior on the cases that motivated each original patch;
  every deleted guard must first become a failing test in the new model.
- Negative (accepted): a single landing touches core ABI, both engines, and the
  Wayland frontend at once — a large, non-bisectable commit relative to the
  current per-file structure.

## Follow-up

ADR-0003 is marked *Superseded by ADR-0011*. The companion explanation docs
([Composition State Machine](../explanation/composition-state-machine.md),
[Timing Model](../explanation/timing-model.md),
[Lifecycle Resilience](../explanation/lifecycle-resilience.md)) describe the
reduce+diff model as the design of record; the code migration (core ABI, the
`basic` and `librime` engines, and the daemon Wayland frontend, landed together)
is the remaining work tracked separately.
