# ADR-0001: Record Architecture Decisions

- **Status**: Accepted
- **Date**: 2026-04-25
- **Deciders**: Project maintainers

## Context

Typio is a multi-component Wayland input method daemon with a nontrivial frontend state machine, plugin engine ABI, D-Bus control surface, and GTK control panel. As the project grows, design choices about state ownership, timing models, and protocol integration accumulate. Without explicit records, the reasoning behind those choices is lost and must be rediscovered by new contributors.

## Decision

This project uses Architecture Decision Records (ADRs) stored in `docs/adr/`.

- Each ADR is numbered sequentially and is append-only after acceptance.
- To change a past decision, write a new ADR that supersedes the old one and update the old ADR's status field only.
- ADRs are short (ideally one page) and focus on the context, decision, alternatives, and consequences.

## Alternatives considered

- **Inline design comments in code**: Rejected. Code comments describe *what* the code does, not *why* a larger design choice was made. They also drift out of date.
- **Long-form architecture documents only**: Rejected. While explanation docs exist, they are mutable. ADRs provide an immutable anchor for specific decisions.

## Consequences

- Positive: New contributors can understand why key design boundaries exist without reading the entire commit history.
- Positive: Reviewers can require an ADR for architectural changes, creating a lightweight gate.
- Trade-off: Maintainers must remember to write ADRs for significant decisions.
- Negative (accepted): A small amount of overhead for each significant architectural change.
