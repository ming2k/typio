# Testing Locally

This guide is for building Typio from a checkout and verifying it on a local
machine without installing it permanently.

## Required Dependencies

- CMake 3.20+
- C11 and C++17 compiler
- `pkg-config`
- Wayland client development files
- `xkbcommon` development files
- `wayland-scanner`
- Vulkan, FreeType, HarfBuzz, and fontconfig development files
- `glslangValidator` for compiling bundled Flux shaders

Optional:

- `librime` for `BUILD_RIME_ENGINE=ON`
- `gtk4` for `BUILD_CONTROL_PANEL=ON`
- `dbus-1` for `ENABLE_STATUS_BUS=ON` or `ENABLE_SYSTRAY=ON`

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

Also install your distribution's Vulkan, FreeType, HarfBuzz, fontconfig, and
glslang packages. Install GTK4, D-Bus, or Rime development packages only when
enabling the matching CMake options.

## Configure and Build

Default local verification build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Build with the optional control panel:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_CONTROL_PANEL=ON
cmake --build build
./build/src/apps/control/typio-control
```

Build with the optional Rime engine:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_RIME_ENGINE=ON
cmake --build build
```

## Run Automated Tests

Run the full test suite:

```bash
ctest --test-dir build --output-on-failure
```

Useful individual binaries:

```bash
./build/tests/test_core
./build/tests/test_config
./build/tests/test_engine_manager
./build/tests/test_status_bus
```

When `BUILD_RIME_ENGINE=ON`, the build also includes:

```bash
./build/tests/test_rime_engine
```

## Run from the Build Tree

Runtime checks require a Wayland session where:

- the compositor exposes `zwp_input_method_manager_v2`
- applications and the compositor provide a working `zwp_text_input_manager_v3` path
- no other input method daemon already owns the seat

Run the daemon without installing:

```bash
./build/src/apps/daemon/typio --engine basic --verbose
```

List available engines from the build tree:

```bash
./build/src/apps/daemon/typio --engine-dir ./build/engines --list
```

Run with a plugin engine built into `./build/engines`:

```bash
./build/src/apps/daemon/typio --engine-dir ./build/engines --engine rime --verbose
```

Common build-tree outputs:

- `./build/src/apps/daemon/typio`
- `./build/src/apps/control/typio-control` when `BUILD_CONTROL_PANEL=ON`
- `./build/engines/*.so` for plugin engines

## Verify Install Layout

A staging install is useful for checking packaged paths without touching system
directories:

```bash
cmake --install build --prefix /tmp/typio-staging
/tmp/typio-staging/bin/typio --version
/tmp/typio-staging/bin/typio --list
rm -rf /tmp/typio-staging
```

Use a system-wide install only when you intentionally want Typio installed:

```bash
sudo cmake --install build --prefix /usr/local
```
