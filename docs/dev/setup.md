# Developer Setup

This document is for contributors who will modify Typio source code. **If you only want to use Typio**, see the [Getting Started tutorial](../tutorials/01-getting-started.md) instead.

## Requirements

- Meson 1.0+ (primary build system)
- Ninja 1.10+
- C23 and C++17 compiler
- Rust toolchain (latest stable `rustc` + `cargo`)
- `pkg-config`
- Wayland client development files
- `xkbcommon` development files
- `wayland-scanner`
- Vulkan, FreeType, HarfBuzz, and fontconfig development files
- `glslangValidator`

Optional:

- `librime` for `build_rime_engine=true`
- `gtk4` for `build_control_panel=true`
- `dbus-1` for `enable_status_bus=true` or `enable_systray=true`

## External dependencies

| Dependency | Source | Resolved version | You need to install it? |
|---|---|---|---|
| **Rust core** (`core/`) | Cargo (Meson `custom_target`) | latest stable | **Yes** — install `rustc` + `cargo` |
| **flux** (rendering framework) | Meson subproject (git fallback) | `44de7ec` (v0.0.1) | **No** — resolved automatically when `subprojects/` is present |

flux is resolved as a Meson subproject when `subprojects/flux.wrap` (or a local `subprojects/flux/` checkout) is present. If the subproject is unavailable, the build continues with candidate popup rendering disabled (stubs are used).

System libraries are discovered via `pkg-config`.  Meson does not enforce upper-bound versions, but the project is regularly tested against the packages shipped in the latest Arch Linux and Fedora releases.

### Hybrid C/Rust architecture

Typio uses a hybrid architecture:

- **Rust** — The core library (`core/`, crate `typio-core`): instance lifecycle, config parsing and schema, input context state, engine ABI/manager/labels, key-event types, logging sink, string utilities, and Rime schema discovery.
- **C / C++** — Everything else: the Wayland frontend (`daemon/wayland/`), engine plugin implementations (`engines/`), Vulkan/flux rendering, D-Bus status surfaces, GTK control panel.

The hand-written C headers in `core/include/typio/*.h` are the ABI contract — the single source of truth. Rust implements matching `#[no_mangle] pub extern "C"` functions. Meson invokes `cargo build` automatically (release profile for `release`/`minsize` buildtype; debug profile otherwise — keeping memory and link time low during iteration) and whole-archive-links the resulting `libtypio_core.a` into `libtypio-core`. No manual Cargo invocation is required during normal development.

When modifying Rust code, edits are picked up automatically on the next `ninja -C build` because the `custom_target` depends on `Cargo.toml` and source files.

For the concrete distro package names required for a default build, see
[Getting Started: Prerequisites](../tutorials/01-getting-started.md).

## Clone and build

Use a debug build with compile commands when editing code:

```bash
meson setup build --buildtype=debug
ninja -C build
```

For a full debug build with every optional feature turned on — useful before pushing changes that may touch the engine, voice, or control-panel paths:

```bash
meson setup build \
  --buildtype=debug \
  -Dbuild_rime_engine=true \
  -Dbuild_mozc_engine=true \
  -Dbuild_whisper=true \
  -Dbuild_sherpa_onnx=true \
  -Dbuild_control_panel=true \
  -Denable_systray=true
ninja -C build
```

## Run tests

```bash
meson test -C build --print-errorlogs
```

For isolated D-Bus runs (sanitizer and CI-like):

```bash
dbus-run-session -- meson test -C build --print-errorlogs
```

## Run the daemon while iterating

```bash
./build/daemon/typio-daemon --engine basic --verbose
```

For plugin engine work, point the daemon at the build-tree engine directory:

```bash
./build/daemon/typio-daemon --engine-dir ./build/engines --engine rime --verbose
```

## Meson options

| Option | Default | Meaning |
|--------|---------|---------|
| `build_basic_engine` | `true` | Build the built-in basic keyboard engine |
| `build_rime_engine` | `false` | Build the optional `librime` engine plugin |
| `build_mozc_engine` | `false` | Build the optional Mozc engine plugin |
| `build_control_panel` | `false` | Build the `typio-control` GTK4 control panel |
| `build_tests` | `true` | Build unit and integration tests |
| `build_whisper` | `false` | Build the Whisper voice backend |
| `build_sherpa_onnx` | `false` | Build the Sherpa-ONNX voice backend |
| `enable_wayland` | `true` | Enable the Wayland frontend |
| `enable_status_bus` | `true` | Enable the D-Bus runtime status/control interface |
| `enable_systray` | `false` | Enable StatusNotifierItem support |

## Project layout

See [project-layout.md](project-layout.md) for a tour of the source tree.

## Submitting changes

See the [Pull Request Checklist](../../CONTRIBUTING.md#pull-request-checklist) in `CONTRIBUTING.md`.
