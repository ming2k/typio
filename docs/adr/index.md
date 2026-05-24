# Architecture Decision Records

ADRs are append-only records of significant design decisions. Once accepted, they are never edited — only superseded by a new ADR.

| ADR | Title | Status |
|-----|-------|--------|
| [ADR-0001](0001-record-architecture-decisions.md) | Record Architecture Decisions | Accepted |
| [ADR-0002](0002-plugin-engine-abi-dual-category.md) | Plugin engine ABI with dual-category (keyboard/voice) slots | Accepted |
| [ADR-0003](0003-keyboard-grab-lifecycle.md) | Keyboard grab lifecycle | Superseded by ADR-0011 |
| [ADR-0004](0004-event-loop-scheduling-and-watchdog.md) | Event-loop scheduling and watchdog design | Accepted |
| [ADR-0005](0005-unified-panel-backend.md) | Unified panel backend for candidate and status UI | Accepted |
| [ADR-0006](0006-resilient-candidate-popup-present.md) | Resilient candidate-popup GPU present | Accepted |
| [ADR-0007](0007-wayland-input-method-v2.md) | Wayland input method v2 | Accepted |
| [ADR-0008](0008-dbus-adapter-over-status-service.md) | D-Bus adapter over TypioStatusService | Accepted |
| [ADR-0009](0009-split-daemon-and-cli-binaries.md) | Split daemon and CLI into separate binaries | Superseded by ADR-0010 |
| [ADR-0010](0010-typio-daemon-complete-separation.md) | Complete separation of `typio` CLI and `typio-daemon` | Accepted |
| [ADR-0011](0011-composition-and-lifecycle-rewrite.md) | Composition and session-lifecycle greenfield rewrite | Accepted |

## Looking for something else?

- Current design docs: [Explanation](../explanation/)
- Developer docs: [dev/](../dev/)
