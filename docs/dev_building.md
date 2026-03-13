# Building from Source

## Required Dependencies

- CMake 3.16+
- C11 compiler
- `pkg-config`
- `wayland-client`
- `xkbcommon`
- `wayland-scanner`

Also required for the default build:

- `librime`

Optional:

- `gtk4` for `BUILD_CONTROL_PANEL=ON`
- `dbus-1` for `ENABLE_STATUS_BUS=ON`
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

The default build also includes:

```bash
./build/tests/test_rime_engine
```

## Run from the Build Tree

During development, run directly from the build tree without installing:

```bash
./build/src/server/typio --engine-dir ./build/engines --engine rime --verbose
```

`BUILD_RPATH` is configured so that all shared libraries are found automatically.

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
| `BUILD_SERVER` | `ON` | Build the `typio` daemon |
| `BUILD_CONTROL_PANEL` | `OFF` | Build the `typio-control` GTK4 control panel |
| `BUILD_TESTS` | `ON` | Build unit tests |
| `BUILD_BASIC_ENGINE` | `ON` | Build the built-in basic keyboard engine |
| `ENABLE_WAYLAND` | `ON` | Enable the Wayland frontend |
| `ENABLE_STATUS_BUS` | `ON` | Enable the D-Bus runtime status/control interface |
| `ENABLE_SYSTRAY` | `ON` | Enable D-Bus StatusNotifierItem support |
| `BUILD_EXAMPLES` | `OFF` | Reserved for future maintained examples |
| `BUILD_RIME_ENGINE` | `ON` | Build the default `librime` engine plugin |

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

Debug build with control panel but no tray icon:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_CONTROL_PANEL=ON -DENABLE_SYSTRAY=OFF
cmake --build build
```

Run from build tree with Rime:

```bash
./build/src/server/typio --engine-dir ./build/engines --engine rime --verbose
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
If you intentionally build with `-DBUILD_RIME_ENGINE=OFF`, the `librime` package is no longer required.
