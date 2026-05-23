# Developer Setup

This document is for contributors who will modify Typio source code. **If you only want to use Typio**, see the [Getting Started tutorial](../tutorials/01-getting-started.md) instead.

## Requirements

- CMake 3.20+
- C11 and C++17 compiler
- `pkg-config`
- Wayland client development files
- `xkbcommon` development files
- `wayland-scanner`
- Vulkan, FreeType, HarfBuzz, and fontconfig development files
- `glslangValidator`

Optional:

- `librime` for `BUILD_RIME_ENGINE=ON`
- `gtk4` for `BUILD_CONTROL_PANEL=ON`
- `dbus-1` for `ENABLE_STATUS_BUS=ON` or `ENABLE_SYSTRAY=ON`

## External dependencies

| Dependency | Source | Resolved version | You need to install it? |
|---|---|---|---|
| **flux** (rendering framework) | ExternalProject (Meson) | `44de7ec` (v0.0.1) | **No** — built automatically |

flux is built automatically during the CMake build step via Meson + Ninja.
A sibling directory `../flux` is used automatically if present; otherwise
the source is cloned from `https://github.com/ming2k/flux.git`.
To point at a different checkout, set the environment variable `FLUX_SOURCE_DIR`
or pass `-DFLUX_SOURCE_DIR=/path/to/flux` to CMake.

System libraries are discovered via `pkg-config`.  CMake does not enforce
upper-bound versions, but the project is regularly tested against the
packages shipped in the latest Arch Linux and Fedora releases.

For the concrete distro package names required for a default build, see
[Getting Started: Prerequisites](../tutorials/01-getting-started.md).

## Clone and build

Use a debug build with compile commands when editing code:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

For a full debug build with every optional feature turned on — useful before pushing changes that may touch the engine, voice, or control-panel paths:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DBUILD_RIME_ENGINE=ON \
  -DBUILD_MOZC_ENGINE=ON \
  -DBUILD_WHISPER=ON \
  -DBUILD_SHERPA_ONNX=ON \
  -DBUILD_CONTROL_PANEL=ON \
  -DENABLE_SYSTRAY=ON
cmake --build build
```

## Run tests

```bash
ctest --test-dir build --output-on-failure
```

For isolated D-Bus runs (sanitizer and CI-like):

```bash
dbus-run-session -- ctest --test-dir build --output-on-failure
```

## Run the daemon while iterating

```bash
./build/src/apps/typio/typio --engine basic --verbose
```

For plugin engine work, point the daemon at the build-tree engine directory:

```bash
./build/src/apps/typio/typio --engine-dir ./build/engines --engine rime --verbose
```

## CMake options

| Option | Default | Meaning |
|--------|---------|---------|
| `BUILD_SHARED_LIBS` | `ON` | Build `typio-core` as a shared library for plugin use |
| `BUILD_CONTROL_PANEL` | `OFF` | Build the `typio-control` GTK4 control panel |
| `BUILD_TESTS` | `ON` | Build unit and integration tests |
| `BUILD_BASIC_ENGINE` | `ON` | Build the built-in basic keyboard engine |
| `BUILD_RIME_ENGINE` | `OFF` | Build the optional `librime` engine plugin |
| `BUILD_MOZC_ENGINE` | `OFF` | Build the optional Mozc engine plugin |
| `ENABLE_WAYLAND` | `ON` | Enable the Wayland frontend |
| `ENABLE_STATUS_BUS` | `ON` | Enable the D-Bus runtime status/control interface |
| `ENABLE_SYSTRAY` | `OFF` | Enable StatusNotifierItem support |
| `ENABLE_ASAN` | `OFF` | Enable AddressSanitizer |
| `ENABLE_UBSAN` | `OFF` | Enable UndefinedBehaviorSanitizer |

## Project layout

See [project-layout.md](project-layout.md) for a tour of the source tree.

## Submitting changes

See the [Pull Request Checklist](../../CONTRIBUTING.md#pull-request-checklist) in `CONTRIBUTING.md`.
