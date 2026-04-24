# Development

This guide is for people changing Typio's source code. If you only want to
build and run Typio locally to verify behavior, use [Testing Locally](testing.md).

## Source Layout

- `src/core/` contains the shared runtime library and public headers.
- `src/apps/typio/` contains the daemon, CLI, Wayland frontend, status bus,
  tray integration, notifications, and voice plumbing.
- `src/apps/control/` contains the optional GTK4 control panel.
- `src/engines/` contains built-in and optional input engine implementations.
- `tests/` contains unit and integration test binaries.
- `external/flux/` is the bundled Flux renderer, built as a static library.

## Development Loop

Use a debug build with compile commands when editing code:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the daemon directly from the build tree while iterating:

```bash
./build/src/apps/daemon/typio --engine basic --verbose
```

For plugin engine work, point the daemon at the build-tree engine directory:

```bash
./build/src/apps/daemon/typio --engine-dir ./build/engines --engine rime --verbose
```

## CMake Options

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

## Sanitizer Builds

Use the helper script for the standard ASan/LSan configuration:

```bash
bash scripts/run_asan.sh
```

Manual sanitizer build:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=1:suppressions=$PWD/tests/asan_suppressions.txt \
LSAN_OPTIONS=suppressions=$PWD/tests/asan_suppressions.txt \
ctest --test-dir build-asan --output-on-failure
```

## Test Ownership

Add or update tests when changing:

- config parsing or schema metadata
- engine manager behavior
- input context commit/preedit semantics
- Wayland lifecycle, key routing, repeat, or startup guard behavior
- candidate popup layout, rendering, or state classification
- public APIs under `src/core/include/typio`

Prefer small state-policy tests for Wayland behavior. Do not rely only on
manual compositor testing when a bug can be reduced to a helper or state model.

## Style

- Use C11 for C code and C++17 where C++ is already required.
- Keep public API names in the `typio_*` / `Typio*` style already used by the repo.
- Prefer local helpers and direct data flow over broad abstractions.
- Document non-obvious behavior in headers or near complex state transitions.
- Keep generated protocol and renderer details behind narrow module boundaries.
