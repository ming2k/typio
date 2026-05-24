# Testing

This document is for contributors. It covers how to run and write tests.

## Run the test suite

```bash
meson test -C build --print-errorlogs
```

Run with an isolated D-Bus session when validating status-bus, tray, or CI-like behavior:

```bash
dbus-run-session -- meson test -C build --print-errorlogs
```

Run sanitizer coverage:

```bash
meson setup build-asan --buildtype=debug -Db_sanitize=address,undefined
ninja -C build-asan
meson test -C build-asan --print-errorlogs
```

Use the helper script for the standard ASan/LSan configuration:

```bash
bash scripts/run_asan.sh
```

Manual sanitizer build:

```bash
meson setup build-asan --buildtype=debug -Db_sanitize=address
ninja -C build-asan
ASAN_OPTIONS=detect_leaks=1:suppressions=$PWD/tests/asan_suppressions.txt \
LSAN_OPTIONS=suppressions=$PWD/tests/asan_suppressions.txt \
dbus-run-session -- meson test -C build-asan --print-errorlogs
```

Use `dbus-run-session` for sanitizer and CI-like runs so status-bus and tray tests get an isolated session bus instead of depending on the developer's desktop session.

## Useful individual binaries

```bash
./build/tests/test_core
./build/tests/test_config
./build/tests/test_engine_manager
./build/tests/test_status_bus
```

When `build_rime_engine=true`, the build also includes:

```bash
./build/tests/test_rime_engine
```

## Test ownership

Add or update tests when changing:

- config parsing or schema metadata
- engine manager behavior
- input context commit/preedit semantics
- Wayland lifecycle, key routing, repeat, or startup guard behavior
- runtime config reload, config-watch debounce, or event-loop scheduling
- voice service state transitions, reload deferral, or completion dispatch
- status/tray D-Bus dispatch loops
- candidate popup layout, rendering, or state classification
- public APIs under `core/include/typio`

Prefer small state-policy tests for Wayland behavior. Do not rely only on manual compositor testing when a bug can be reduced to a helper or state model.

## Style

- Use C23 for C code and C++17 where C++ is already required.
- Use Rust 2021 edition for `core/` (crate `typio-core`).
- Keep public API names in the `typio_*` / `Typio*` style already used by the repo.
- Prefer local helpers and direct data flow over broad abstractions.
- Document non-obvious behavior in headers or near complex state transitions.
- Keep generated protocol and renderer details behind narrow module boundaries.
