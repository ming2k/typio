# Building from Source

## Required Dependencies

- CMake 3.20+
- C11 and C++17 compiler (GCC 9+ or Clang 10+)
- `pkg-config`
- `wayland-client`
- `xkbcommon`
- `wayland-scanner`
- **Skia** (Must be built and placed in `external/skia`)

### Setting up Skia

Typio now uses Skia for hardware-accelerated rendering. You must provide a compiled Skia library in `external/skia/out/Release/libskia.a`.

1. Clone Skia into `external/skia`.
2. Follow Skia's build instructions to produce a Release build with `is_official_build=true`.
3. Ensure headers are available in `external/skia/include`.

Optional:

- `librime` for `BUILD_RIME_ENGINE=ON`
- `gtk4` for `BUILD_CONTROL_PANEL=ON`
- `dbus-1` for `ENABLE_STATUS_BUS | ON | Enable the D-Bus runtime status/control interface (integrated into `typio`) |
- `dbus-1` for `ENABLE_SYSTRAY=ON`

## Configure and Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Run the Tests

```bash
ctest --test-dir build --output-on-failure
```

Individual binaries:

```bash
./build/tests/test_core
./build/tests/test_config
./build/tests/test_engine_manager
```

When `BUILD_RIME_ENGINE=ON`, the build also includes:

```bash
./build/tests/test_rime_engine
```

## Run from the Build Tree

During development, run directly from the build tree without installing:

```bash
./build/src/apps/daemon/typio --engine basic --verbose
```

`BUILD_RPATH` is configured so that all shared libraries are found automatically.

Common build-tree outputs:

- `./build/src/apps/daemon/typio`

- `./build/src/apps/control/typio-control` when `BUILD_CONTROL_PANEL=ON`
- `./build/engines/*.so` for plugin engines

The source tree mirrors this split:

- `src/core/` for shared library code
- `src/apps/` for executable entrypoints
- `src/engines/` for engine implementations

## Install

Install is only needed for packaging or verifying the installed file layout.

Staging install (disposable, no root needed):

```bash
cmake --install build --prefix /tmp/typio-staging
/tmp/typio-staging/bin/typio --verbose
rm -rf /tmp/typio-staging
```

System-wide install:

```bash
sudo cmake --install build --prefix /usr/local
```

## Important CMake Options

| Option | Default | Meaning |
|--------|---------|---------|
| `BUILD_SHARED_LIBS` | `ON` | Build `typio-core` as a shared library for plugin use |
| `BUILD_CONTROL_PANEL` | `OFF` | Build the `typio-control` GTK4 control panel |
| `BUILD_TESTS` | `ON` | Build unit tests |
| `BUILD_BASIC_ENGINE` | `ON` | Build the built-in basic keyboard engine |
| `ENABLE_WAYLAND` | `ON` | Enable the Wayland frontend |
| `ENABLE_STATUS_BUS | ON | Enable the D-Bus runtime status/control interface (integrated into `typio`) |
| `ENABLE_SYSTRAY` | `OFF` | Enable D-Bus StatusNotifierItem support |
| `BUILD_RIME_ENGINE` | `OFF` | Build the optional `librime` engine plugin |

## Suggested Developer Invocations

Debug build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

Debug build with the default plugin set:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

ASAN/LSAN debug build for leak and memory-corruption hunting:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=1:suppressions=$PWD/tests/asan_suppressions.txt \
LSAN_OPTIONS=suppressions=$PWD/tests/asan_suppressions.txt \
ctest --test-dir build-asan --output-on-failure
```

Or use the helper script:

```bash
bash scripts/run_asan.sh
```

Debug build with control panel but no tray icon:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_CONTROL_PANEL=ON -DENABLE_SYSTRAY=OFF
cmake --build build
```

Run from build tree with the built-in engine:

```bash
./build/src/apps/daemon/typio --engine basic --verbose
```

## Distro Packages

Debian/Ubuntu:

```bash
sudo apt install build-essential cmake pkg-config libwayland-dev libxkbcommon-dev wayland-protocols
```

Arch Linux:

```bash
sudo pacman -S base-devel cmake pkgconf wayland libxkbcommon wayland-protocols
```

Fedora:

```bash
sudo dnf install gcc cmake pkgconf-pkg-config wayland-devel libxkbcommon-devel wayland-protocols-devel
```

If you enable the D-Bus status bus or systray support, also install the `dbus-1` development package for your distribution.
If you build the control panel, also install the `gtk4` development package for your distribution.
If you build with `-DBUILD_RIME_ENGINE=ON`, install the `librime` development package for your distribution.
