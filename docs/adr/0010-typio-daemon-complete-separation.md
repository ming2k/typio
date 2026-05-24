# ADR-0010: Complete separation of `typio` CLI and `typio-daemon`

- **Status**: Accepted
- **Date**: 2026-05-24
- **Deciders**: ming2k

## Context

ADR-0009 split the old dual-mode `typio` into two binaries, but kept `typio daemon` as a forwarding wrapper that `exec`s the daemon binary. This preserved a convenience feature at the cost of breaking encapsulation:

- The daemon binary remained discoverable and directly runnable by users.
- The CLI was still responsible for daemon startup, mixing client and launcher concerns.
- `typio daemon` was essentially a thin shell around `exec daemon`, providing little value.

## Decision

Fully separate the two binaries. The CLI **never** starts the daemon.

1. **`typio-daemon`** (C, under `daemon/`)
   - The only background service binary.
   - No subcommands. Directly parses daemon flags (`--config`, `--engine`, `--verbose`, etc.).
   - Installed to `libexec/typio/` so it is not on the user PATH.
   - Started by systemd, desktop autostart, or manual invocation.

2. **`typio`** (Rust, under `cli/`)
   - Pure client. Only communicates with an already-running daemon.
   - If the daemon is not running, prints an error and exits.
   - Never attempts to locate, fork, or exec `typio-daemon`.

## Alternatives considered

- **Keep `typio daemon` as a launcher**: Rejected. It leaks the daemon binary into the user interface and gives the CLI an inappropriate responsibility.
- **Embed daemon logic into `typio` via FFI**: Rejected. It would force the CLI to link Wayland, Vulkan, D-Bus, etc., defeating the purpose of the split.
- **Use systemd socket activation**: Rejected. Over-engineered for the current scope and not portable to non-systemd environments.

## Consequences

- Positive: Cleanest possible separation of concerns. CLI is pure client; daemon is pure server.
- Positive: Users cannot accidentally bypass the intended startup path.
- Positive: systemd service and desktop files call `typio-daemon` directly, no wrapper indirection.
- Trade-off: Users must start the daemon themselves (or rely on systemd/autostart) before using `typio` commands.
- Trade-off: Two distinct binary names (`typio` and `typio-daemon`) to document and package.
