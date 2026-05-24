# ADR-0009: Split daemon and CLI into separate binaries

- **Status**: Superseded by ADR-0010
- **Date**: 2026-05-24
- **Deciders**: ming2k

## Context

The `typio` executable historically acted as a dual-mode binary:
- `typio daemon` started the Wayland IME daemon
- `typio status`, `typio engine`, etc. acted as a lightweight D-Bus client

This design was convenient for distribution (one binary to install) but mixed unrelated concerns:
- The daemon links heavy platform dependencies: Wayland, Vulkan (via flux), XKB, PipeWire, D-Bus, GTK, FreeType, HarfBuzz
- The client needs none of these; it only talks to the daemon
- CLI features (colored output, structured logging, shell completion) are much easier to build in Rust than in C
- Every daemon bugfix or feature release forced a client redeploy, and vice versa

## Decision

Split into two binaries with clear boundaries:

1. **`daemon`** (C, under `daemon/`)
   - Pure daemon. No client code.
   - Owns Wayland frontend, IPC bus, status bus, tray, voice.
   - Listens on a UDS socket (`$XDG_RUNTIME_DIR/typio/daemon.sock`).

2. **`typio`** (Rust, under `cli/`)
   - Command-line interface.
   - `typio daemon` execs the `daemon` binary and forwards arguments.
   - All other subcommands (`status`, `engine`, `config`, `stop`, etc.) communicate with the running daemon over UDS (JSON-RPC 2.0), falling back to D-Bus if UDS is unavailable.
   - No dependency on `typio-core` or any C platform libraries.

The IPC protocol (method names, property names, socket path convention) is shared implicitly via the UDS surface already implemented in `daemon/ipc/`. Both sides use the same JSON-RPC 2.0 framing with a 4-byte big-endian length prefix.

## Alternatives considered

- **Keep the single C binary**: Rejected. It blocks CLI improvements and forces client users to install daemon dependencies.
- **Put the CLI inside `core-rs` as a second binary target**: Rejected. `core-rs` is the *library* crate; adding a CLI binary there would couple the client to the core build and confuse the dependency graph.
- **Name the CLI `typioctl` and the daemon `typiod`**: Rejected. The user preferred `typio` for the CLI (familiar entry point) and `daemon` for the daemon (short, clear).

## Consequences

- Positive: CLI can evolve independently (clap derive, serde_json, tracing, etc.).
- Positive: Daemon binary shrinks because client_main code is removed.
- Positive: Packaging flexibility — headless servers can skip the CLI, management scripts can skip the daemon.
- Trade-off: Two binaries to install instead of one. CMake and packaging scripts must handle both.
- Trade-off: The IPC protocol is now a public contract. Changes must be coordinated across C (daemon) and Rust (CLI).
- Negative (accepted): Breaking change for users who previously called `./typio` directly to start the daemon. They must now use `typio daemon` (which forwards) or `daemon` directly.
