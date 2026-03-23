# State Management

This document defines the preferred state-management model for Typio.

It exists because Typio now has multiple control surfaces, multiple engine
types, and a growing set of settings that influence both persisted config and
runtime behaviour. The main failure mode is allowing those layers to drift
until one UI starts guessing state that actually belongs to the daemon.

## Goal

Every user-visible state value in Typio must belong to exactly one of these
domains:

- `persisted config`
  Canonical user intent stored in `typio.toml`
- `runtime state`
  Current daemon state exposed over the status bus
- `local staged edits`
  Client-side edits not yet confirmed by the daemon
- `view state`
  Temporary widget state with no authority of its own

The design target is simple:

- the daemon owns persisted config and runtime truth
- control surfaces may stage edits, but may not invent truth
- runtime and config must never be silently substituted for each other

## Ownership Rules

### Persisted Config

Owned by the daemon.

Rules:

- `typio.toml` is the only persistent config store
- only the daemon writes it
- control surfaces mutate config through daemon APIs such as
  `SetConfigText`
- config defaults happen daemon-side

Examples:

- `default_engine`
- `default_voice_engine`
- `shortcuts.voice_ptt`

### Runtime State

Owned by the daemon and published over the D-Bus status interface.

Rules:

- runtime state must be consumable without reading local files
- control surfaces must display runtime state from D-Bus, not by inferring
  from config
- runtime state may differ temporarily from persisted config during startup,
  reload, failure, or async engine switching

Current runtime properties include:

- `ActiveEngine`
- `ActiveVoiceEngine`
- `AvailableEngines`
- `ActiveEngineState`
- `RimeSchema`
- `ConfigText`

### Local Staged Edits

Owned by a single client instance such as `typio-control`.

Rules:

- local stage starts from daemon `ConfigText`
- local stage may diverge from daemon state while an autosave is pending
- stale daemon replies must not overwrite newer staged edits
- local stage is disposable; it is not a source of truth outside that client

### View State

Owned by the widget tree.

Rules:

- widget state must be reconstructed from runtime state and staged config
- widget state must never be written back unless the corresponding domain is
  known
- unknown state must remain unknown; do not guess the first dropdown item

## Binding Rules

Each control should bind to exactly one authority for reads and one authority
for writes.

Patterns:

- runtime selector
  Read from D-Bus runtime property, write to daemon method or config edit path
- config editor
  Read from staged `ConfigText`, write back to staged `ConfigText`
- mixed selector
  Show runtime truth in the control, but stage persistent intent separately

Examples:

- keyboard engine selector
  Read runtime from `ActiveEngine`
  Persist via `default_engine`
- voice backend selector
  Read runtime from `ActiveVoiceEngine`
  Persist via `default_voice_engine`
- Rime schema selector
  Read runtime selection from `RimeSchema`
  Read available choices from shared schema discovery
  Persist via XDG state and `SetRimeSchema`

## Control-State Bindings

`typio-control` now has a small binding layer for high-risk selector state.

Current `ControlStateBinding` responsibilities:

- identify the persisted config key
- identify the widget that renders the state
- map string values to dropdown indices and back
- declare a `value source policy`
- optionally provide an `options refresh` callback

Current value-source policies:

- `FROM_CONFIG`
  Read the selected value from staged daemon `ConfigText`
- `FROM_RUNTIME`
  Read the selected value from the daemon runtime property mirrored by the
  config key
- `RUNTIME_THEN_CONFIG`
  Prefer daemon runtime state, but fall back to staged config when runtime is
  temporarily unavailable

Current selector mapping in `typio-control`:

- keyboard engine
  config key: `default_engine`
  value source: `FROM_RUNTIME`
  options source: `AvailableEngines`
- voice backend
  config key: `default_voice_engine`
  value source: `RUNTIME_THEN_CONFIG`
  options source: `AvailableEngines`
- Rime schema
  state key: `schema` in `rime-state.toml`
  value source: `FROM_RUNTIME`
  options source: `typio_rime_schema_list_load()`

This binding layer exists to prevent control-surface code from repeating
manual logic for:

- choosing between runtime and config state
- guessing fallback selections
- re-implementing the same dropdown selection rules in multiple places

New selector-style state should prefer extending this layer rather than adding
new ad-hoc sync code in `control_config.c`.

## Schema Guidance

The config schema table is the registry for persisted fields. It should also
carry enough metadata to tell control surfaces whether a field has a direct
runtime counterpart.

Current rule:

- fields with a direct runtime mirror should set `runtime_property`

Current examples:

- `default_engine` -> `ActiveEngine`
- `default_voice_engine` -> `ActiveVoiceEngine`

This is intentionally narrow. Do not attach a runtime property unless there is
one stable daemon property that represents the field's live value.

## Best-Practice Workflow For New State

When adding a new stateful feature:

1. decide whether it is persisted, runtime, or both
2. if persisted, add it to `config_schema.c`
3. if runtime, expose it through the status bus
4. if a persisted key has a direct runtime mirror, set `runtime_property`
5. make control surfaces read from the correct authority
6. add regression tests for startup, refresh, and delayed apply cases

For selector-style state such as engine choice, voice backend, and Rime
schema, the minimum regression bar is:

- `options -> UI`
  refreshed options must include any explicit unselected state and preserve a
  configured value even if discovery is incomplete
- `config -> UI`
  loading staged config must select the matching option in the widget
- `UI -> config`
  user selection must write the correct persisted key, including removing the
  key when the selector is explicitly unselected

For runtime-driven selectors such as `default_engine`, also require:

- `runtime/status -> UI`
  the status bus must export the runtime value and the control surface must map
  that runtime value back to the correct internal selector id, not a display
  label

Treat selector tests as belonging to one of three categories:

- `config-driven`
  example: `display.popup_theme`
- `runtime-driven`
  example: `default_engine`
- `mixed`
  example: `default_voice_engine`

Do not reuse a config-selector test pattern for a runtime selector just because
both render as dropdowns. The test contract must match the selector's declared
`ControlStateValueSource`.

## Anti-Patterns

Do not:

- read `typio.toml` directly from a UI
- infer runtime state from config when a daemon property exists
- infer config state from current runtime activation
- write config from widget defaults before the first daemon seed
- replace unknown state with the first available dropdown option
- maintain separate ad-hoc lists of runtime property names in different layers
- add selector UIs without tests that cover `options -> UI`, `config -> UI`,
  and `UI -> config`

## Migration Plan

Short-term:

- centralize D-Bus property and method names in shared protocol headers
- annotate persisted config fields that have direct runtime mirrors
- keep expanding regression coverage around startup and async apply

Mid-term:

- add a small runtime-state registry beside the config schema if runtime
  properties become numerous
- move control-surface bindings toward schema-driven and protocol-driven
  tables instead of one-off handwritten mappings

Long-term:

- make new control surfaces consume the same state model without bespoke state
  ownership rules
