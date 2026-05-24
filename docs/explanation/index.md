# Explanation

Understanding-oriented documents that explain *why* Typio works the way it does. These are discursive and may include opinions and trade-offs.

- [Architecture Overview](architecture-overview.md) — High-level structure, components, data flow, and design rules
- [Composition State Machine](composition-state-machine.md) — Abstract key-to-commit pipeline: preedit, candidates, the four `process_key` outcomes, and focus/reset semantics
- [Engine Contract](engine-contract.md) — Why the framework is language-agnostic: the engine↔framework boundary, the two-emits contract (composition + commit), property-bag state, and how Rime, Mozc, basic, and voice all share the same interface
- [Wayland Input Method](wayland-input-method.md) — How Typio implements the `zwp_input_method_v2` protocol family: events as facts, the commit-serial chokepoint, and the derived reduce+diff lifecycle
- [Lifecycle Resilience and Recovery](lifecycle-resilience.md) — Surviving suspend/resume, compositor restarts, and silent grab loss: recovery as a property of the idempotent diff, plus the resume detector, checkpoint durability, and reconnect
- [Config & Runtime Ownership](config-runtime-ownership.md) — Who owns persisted config, runtime state, staged edits, and view state across daemon and control surfaces
- [Timing Model](timing-model.md) — Event ordering, the derived reduce+diff state, the one epoch fence, keyboard safety, and the virtual-keyboard state machine
- [Control Surfaces](control-surfaces.md) — Design rules for `typio-control`, tray menu, and future UI integrations
- [Configuration System](configuration-system.md) — Why the schema table is the single source of truth for all config fields
- [Voice Input Architecture](voice-input.md) — State machine, backend proxy pattern, audio pipeline, and reload semantics

## Looking for something else?

- Learning the basics? See [Tutorials](../tutorials/)
- Trying to accomplish a task? See [How-to guides](../how-to/)
- Looking up a value? See [Reference](../reference/)
- Want to see why a specific decision was made? See [ADR](../adr/)
