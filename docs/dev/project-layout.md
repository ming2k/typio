# Project Layout

The source tree is organized by stable product boundary first:

## `core/`

The core library — platform-agnostic business logic. A Rust crate (`typio-core`) exposing a hand-written C ABI, plus its public headers.

- `Cargo.toml` / `Cargo.lock` — crate manifest. Package: `typio-core`. Static library: `libtypio_core.a`.
- `src/` — Rust sources. Each module corresponds to one ABI area:
  - `config.rs`, `config_schema.rs` — TOML config load/save and static schema
  - `input_context.rs` — input context state management
  - `engine.rs`, `engine_manager.rs`, `engine_label.rs` — engine ABI, registry, loading, switching
  - `instance.rs` — top-level Typio instance lifecycle
  - `event.rs`, `types.rs` — key-event types and ABI primitives
  - `log.rs`, `string.rs` — logging sink and string utilities (C ABI)
  - `rime_schema_list.rs` — Rime schema discovery
  - `build_info.rs` — build version metadata
- `include/typio/` — installed public C headers (hand-written, single source of truth). Includes `log.h` (header-only `static inline typio_log`) and `string.h`.
- `CMakeLists.txt` / `meson.build` — drives `cargo build` (profile follows build type) and wraps the Rust staticlib into the shared `typio-core` library. Meson is the primary build system; CMake is deprecated.

## `daemon/`

The system-facing daemon. Platform-specific adapter layer: Wayland IME host, IPC bus, status bus, tray, and voice plumbing. Translates platform events (Wayland protocol, keyboard grab, D-Bus) into `core` abstractions and translates `core` callbacks back into platform requests. Pure daemon; no client code.

## `control/`

The GTK4 control panel (`typio-control`).

## `engines/`

Built-in and pluggable input-engine implementations.

## `tests/`

Unit and integration test binaries.

## `cli/`

The Rust command-line client (`typio`). Built separately from the daemon, it communicates over UDS (with D-Bus fallback) to query and control a running daemon. The daemon (`typio-daemon`) is started separately.

## Design rationale

Top-level directories sit on one axis: the platform-agnostic core library, the system-facing daemon, the command-line client, the control-panel application, and engine implementations.

The rule the layout encodes is **core owns business logic; daemon owns platform glue**. `core/` knows nothing about Wayland, D-Bus, GTK, X11, Vulkan, or the event loop. `daemon/` knows everything about the platform but delegates all linguistic and configuration decisions to `core`. The C ABI is the narrow boundary between the two.

The core is a Rust crate to get memory and concurrency safety on the parts most easily corrupted; its C ABI keeps every other layer free to remain C/C++ without churn. The CLI is also Rust because it needs no C dependencies and benefits from modern argument parsing and error handling. Public headers live next to the crate so consumers see a single include root (`core/include/typio/`).
