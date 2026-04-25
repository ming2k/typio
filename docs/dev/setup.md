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

## Clone and build

Use a debug build with compile commands when editing code:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
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

## Before submitting a PR

- [ ] All tests pass
- [ ] Build succeeds from a clean tree
- [ ] Sanitizer builds pass if the change touches memory or lifetime boundaries
- [ ] `CHANGELOG.md` updated
- [ ] If architectural change: ADR added
