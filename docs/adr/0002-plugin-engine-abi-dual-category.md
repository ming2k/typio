# ADR-0002: Plugin engine ABI with dual-category (keyboard/voice) slots

- **Status**: Accepted
- **Date**: 2026-05-22
- **Deciders**: Project maintainers

## Context

Typio is a framework host, not a language engine. The actual input logic
(Rime, Mozc, speech recognition) must live behind a stable boundary so engines
can be developed, built, and shipped independently of the daemon — including as
out-of-tree shared objects. Two unrelated input modalities coexist: a primary
keyboard pipeline (composition, candidates, commit) and a secondary voice
pipeline (speech → text). They are selected independently and must not interfere
with each other.

## Decision

Engines are loaded through a small C ABI and managed in two parallel slots.

- An engine shared object exports `typio_engine_get_info` and
  `typio_engine_create`. `TypioEngineInfo` opens with `api_version` and a
  `struct_size` sentinel so the host can reject incompatible plugins and so the
  struct can grow without breaking old binaries.
- `TypioEngineManager` holds exactly one active **keyboard** engine and one
  active **voice** engine. Selecting within one category never evicts the other.
- Engine instances are created lazily on first selection. If creation or
  activation fails, the manager restores the previously active engine in the
  same category.
- Engines own their own `user_data` and all engine-specific runtime state
  (schema, mode, etc.); the host owns protocol hosting and UI.

## Alternatives considered

- **Single flat engine list**: Rejected. Keyboard and voice would share one
  active slot and evict each other, which contradicts running dictation
  alongside typing.
- **Compiled-in engines only**: Rejected. Prevents third-party / out-of-tree
  engines and couples engine release cadence to the daemon.
- **Versionless ABI**: Rejected. The info struct could never gain fields without
  breaking already-built plugins; `api_version` + `struct_size` make evolution
  safe.

## Consequences

- Positive: engines are independently buildable and loadable; the host stays
  language-agnostic.
- Positive: keyboard and voice evolve and fail independently.
- Trade-off: two-category management is more complex than one active engine.
- Negative (accepted): cross-version ABI care is now a permanent obligation —
  fields are append-only and gated by `struct_size`.
