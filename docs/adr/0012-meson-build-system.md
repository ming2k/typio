# ADR-0012: Adopt Meson as Primary Build System

- **Status**: Implemented
- **Date**: 2026-05-25
- **Deciders**: Project maintainers

## Context

Typio has used CMake since inception. Over time the build grew to include:

- C/C++ daemon and engines
- Rust core (built via `cargo` inside CMake custom commands)
- Wayland protocol generation (`wayland_scanner`)
- Protobuf compilation for the Mozc engine
- An external Meson project (`flux`) built via CMake `ExternalProject_Add`

CMake handled this, but the configuration became verbose and the `flux` integration via `ExternalProject_Add` was fragile (requires `meson` + `ninja` in `PATH`, git clone on first build, no native subproject support).

Meson offers first-class support for:
- Rust via `custom_target` (same approach, less boilerplate)
- Native subprojects (git-based fallbacks, wrapping external Meson projects like `flux`)
- Cleaner conditional source lists and dependency handling
- A more readable build definition language for a project of this complexity

## Decision

Adopt Meson as the **sole** build system. All CMake files have been removed in v4.1.0.

Key implementation details:

- Root `meson.build` configures options, dependencies, and subdirectories.
- Rust core is built via `custom_target` invoking `cargo build` (profile follows `buildtype`).
- `flux` is resolved as a Meson subproject (via `subprojects/flux.wrap`) when available; when missing it is gracefully disabled and candidate popup rendering falls back to stubs.
- `.desktop` and `.service` templates use `@TYPIO_DAEMON_DIR@` (replaces the previously broken `@CMAKE_INSTALL_FULL_LIBEXECDIR@`).
- Wayland protocols are generated via `custom_target` + `wayland_scanner`.

## Alternatives considered

- **Keep CMake only**: Rejected. CMake's `ExternalProject_Add` for `flux` is a poor fit; Meson's native subproject support is strictly better for this dependency.
- **Migrate to Cargo workspace with `cargo-c`**: Rejected. The C/C++ surface (daemon, engines, Wayland protocols, GTK control panel) is too large to fit into a Cargo-native build.
- **Use Bazel**: Rejected. Heavyweight, poor distro packaging story, overkill for a desktop daemon.

## Consequences

- Positive: Cleaner build definition; native subproject support for `flux`; easier for downstream packagers (most distros prefer Meson).
- Positive: Fixed long-standing bug where CMake-generated `.desktop`/`.service` files had empty daemon paths because `CMAKE_INSTALL_FULL_LIBEXECDIR` was never defined.
- Positive: Removed dual build system maintenance burden.
- Negative (accepted): Contributors must install `meson` and `ninja` (they were already required implicitly for `flux` under CMake).
