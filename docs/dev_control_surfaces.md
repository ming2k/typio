# Control Surfaces

This document covers Typio's user-facing control surfaces:

- `typio-control`
- the tray menu
- future settings widgets or shell integrations

It replaces the older split between UI-only notes and control-center config
flow notes. The goal is to keep information architecture, state ownership, and
editing rules in one place.

## Scope

Control surfaces have two jobs:

- present runtime state coming from `typio-server`
- let the user stage and apply persistent configuration changes safely

They must not become a second source of truth for runtime or config state.

## Sources Of Truth

Every control surface must keep these layers separate:

- `runtime state`
  Data exposed by the daemon, primarily over the D-Bus status interface.
- `persisted config`
  The authoritative `typio.toml` as seen by the daemon.
- `local draft`
  Unsaved edits staged in a specific UI.
- `view state`
  Widget state such as dropdown selections, switches, and spin buttons.

Rules:

- Runtime state must come from the daemon, not from client-side filesystem
  guesses.
- Persistent edits must start from the daemon's current `ConfigText`.
- Widget state is never authoritative by itself.
- Programmatic refresh must not mutate the local draft or mark it dirty.

## Editing Model

`typio-control` uses a seeded-draft model:

1. wait for the first `ConfigText` from the status bus
2. seed the local draft from that config text
3. let user edits mutate the local draft
4. submit the full staged draft only on `Apply`

Required invariants:

- Before the first successful seed, widget initialization must not write draft
  config.
- During programmatic refresh, all change handlers must be suppressed.
- `Apply` must only submit a seeded draft.
- Default values belong to schema application and daemon-side config reload,
  not to control-surface startup.

## Known Failure Pattern

This class of bug is easy to reintroduce:

1. the control surface starts before the daemon is ready
2. widget setup emits change signals
3. the UI writes a local draft based on widget defaults
4. the user edits one unrelated setting and clicks `Apply`
5. the whole polluted draft overwrites unrelated daemon config

This is how a Rime-schema edit can accidentally reset `default_engine` or
other top-level keys.

## Information Architecture

- Top-level navigation should represent stable product areas such as
  `Display`, `Engines`, and `Shortcuts`.
- Avoid mixing categories and concrete instances in the same navigation layer.
- Engine/backend/model choices belong in dropdowns, not in extra tabs.
- Use at most two navigation levels in the control center.

## Visual Hierarchy

- Treat runtime problems as banners or alerts near the top of the content area.
- Treat unsaved-change state as an action-local hint near `Apply` / `Cancel`,
  not as page content.
- Use page titles sparingly; do not repeat obvious navigation labels.
- Prefer spacing and grouping over extra frames or helper text.

## Component Rules

- Sidebars are for durable navigation.
- Stack switchers are for tightly related sibling views only.
- Buttons are for explicit actions, not for passive refreshes.
- Remove maintenance actions from the main flow if the daemon can refresh
  itself or if the action has no meaningful user decision attached.

## Rime Schema Discovery

Both the tray menu and control center use `typio_rime_schema_list_load()`
from the core library (`typio/rime_schema_list.h`).  This ensures schema
lists are always consistent regardless of which surface the user is
looking at.  Neither surface should implement its own schema file parsing.

## Tray Menu Rules

- The main engine list should contain keyboard engines only.
- Rime schema choices may appear under a Rime-specific submenu because they are
  part of day-to-day keyboard usage.
- Voice controls should stay out of the tray unless they become a primary
  frequent action.

## Documentation And Tests

Any change to control-surface behavior should update:

- this document, if source-of-truth or editing rules change
- user documentation, if visible UI or behavior changes
- regression tests for startup seeding, dirty-state handling, and config apply

Minimum regression coverage to keep:

- startup before the daemon appears must not dirty the local draft
- programmatic dropdown refresh must not rewrite config
- changing one field must not rewrite unrelated top-level settings
- Rime and voice settings must round-trip through daemon `ConfigText`
