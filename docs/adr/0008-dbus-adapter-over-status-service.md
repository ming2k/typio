# ADR-0008: D-Bus adapter over TypioStatusService

- **Status**: Accepted
- **Date**: 2026-05-24
- **Deciders**: ming (maintainer)

## Context

The IPC refactor (replacing D-Bus with UDS for the primary control channel) introduced `TypioStatusService` as a transport-agnostic business-logic layer. Initially, the D-Bus adapter (`daemon/status/status.c`) retained its own full implementation of every method handler (ActivateEngine, NextEngine, ReloadConfig, etc.), duplicating the logic already present in `status_service.c`.

This created "two sets" of business logic:
1. `TypioStatusService` — used by the UDS server (`ipc_bus.c`).
2. `TypioStatusBus` (D-Bus) — used by the D-Bus adapter (`status.c`).

Any change to state semantics (e.g., saving config after engine activation) had to be made in both places, risking drift and subtle behavioural differences between the UDS and D-Bus paths.

## Decision

Make the D-Bus adapter a **thin transport layer** that delegates all business logic to `TypioStatusService`.

- `TypioStatusBus` owns a `TypioStatusService *service` instance.
- D-Bus method handlers extract arguments from `DBusMessage`, marshal them into a JSON `params` object, call `typio_status_service_handle()`, and convert the JSON response back into a D-Bus reply.
- Get/GetAll property handlers remain unchanged (they only read state and already share the same query paths via `append_property_variant`).
- State-controller bindings and runtime-state callbacks are forwarded to the underlying `TypioStatusService` so that both transports share identical notification paths.

## Alternatives considered

- **Keep dual logic paths**: Rejected. Violates DRY and guarantees divergence over time.
- **Move D-Bus into `TypioIpcBus`**: Rejected. The D-Bus adapter is an optional compile-time feature (`HAVE_STATUS_BUS`); coupling it to the always-on UDS bus would complicate conditional compilation and increase the core daemon's dependency surface.

## Consequences

- Positive: Single source of truth for all control/state operations.
- Positive: Bug fixes or behavioural changes apply to both transports automatically.
- Trade-off: D-Bus method handlers now perform an extra JSON marshalling/unmarshalling round-trip. The overhead is negligible for the control-plane message volume.
- Negative (accepted): `status.c` now depends on `tip_json.c`/`tip_protocol.c` for response parsing, slightly increasing the D-Bus adapter's internal coupling.
