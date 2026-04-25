# Project Layout

The source tree is organized by stable product boundary first:

## `src/core/`

Shared library code.

- `include/typio/` — installed public headers and cross-component protocol constants
- `runtime/` — core implementation units that build `typio-core`
- `utils/` — internal support helpers used by the core, daemon, engines, and selected tests

## `src/apps/`

Executable programs.

- `typio/` — the Wayland IME host, the D-Bus command-line control surface, status bus, tray, and voice plumbing
- `control/` — the GTK control panel

## `src/engines/`

Built-in and pluggable input-engine implementations.

## `tests/`

Unit and integration test binaries.

## `external/flux/`

The bundled Flux renderer, built as a static library.

## Design rationale

This keeps top-level `src/` directories on one axis: reusable core, user-facing applications, and engine implementations. It makes it obvious where a new file belongs and reduces cross-directory coupling.
