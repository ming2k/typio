# Project Layout

The source tree is organized by stable product boundary first:

## `src/core/`

The core library. A Rust crate (`typio-core`) exposing a hand-written C ABI, plus its public headers.

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
- `CMakeLists.txt` — drives `cargo build` (profile follows `CMAKE_BUILD_TYPE`) and wraps the Rust staticlib into the shared `typio-core` library.

## `src/apps/`

Executable programs.

- `typio/` — the Wayland IME host, the D-Bus command-line control surface, status bus, tray, and voice plumbing
- `control/` — the GTK control panel

## `src/engines/`

Built-in and pluggable input-engine implementations.

## `tests/`

Unit and integration test binaries.

## Design rationale

Top-level `src/` directories sit on one axis: the reusable core library, user-facing applications, and engine implementations. The core is a Rust crate to get memory and concurrency safety on the parts most easily corrupted; its C ABI keeps every other layer free to remain C/C++ without churn. Public headers live next to the crate so consumers see a single include root (`src/core/include/typio/`).
