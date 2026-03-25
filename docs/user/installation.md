# Installation

## Requirements

- CMake 3.16+
- C11 compiler
- `pkg-config`
- Wayland client development files
- `xkbcommon` development files
- `wayland-scanner`
- a Wayland compositor that exposes `zwp_input_method_manager_v2`
- Wayland applications in that session must use a working `zwp_text_input_manager_v3` path

Optional:

- `librime` development files if you want to build with `BUILD_RIME_ENGINE=ON`
- `dbus-1` development files if you want to build with `ENABLE_SYSTRAY=ON`

## Build

```bash
cmake -S . -B build
cmake --build build
```

`ctest --test-dir build --output-on-failure` is optional if you only want a usable build.

## Try without Installing

You can run Typio directly from the build tree:

```bash
./build/src/server/typio --engine basic --verbose
```

## Install

For a permanent system-wide install:

```bash
sudo cmake --install build --prefix /usr/local
```

To verify the installed layout without touching system directories, use a
disposable staging prefix:

```bash
cmake --install build --prefix /tmp/typio-staging
/tmp/typio-staging/bin/typio --verbose
rm -rf /tmp/typio-staging
```

Installed paths (with `/usr/local` prefix):

- `/usr/local/bin/typio`
- `/usr/local/lib/libtypio-core.so`
- `/usr/local/lib/typio/engines/rime.so`
- `/usr/local/include/typio/*.h`
- `/usr/local/lib/pkgconfig/typio.pc`
- `/usr/local/share/typio/typio.toml.example`
Install also places a desktop autostart entry at
`/etc/xdg/autostart/typio.desktop` so Typio starts automatically with the
desktop session. Override the location with `-DTYPIO_AUTOSTART_DIR=/some/path`
if you need a different target for packaging or testing.

## Useful Build Options

| Option | Default | Purpose |
|--------|---------|---------|
| `BUILD_SHARED_LIBS` | `ON` | Build `typio-core` as a shared library |
| `BUILD_TESTS` | `ON` | Build the automated tests |
| `BUILD_BASIC_ENGINE` | `ON` | Build the built-in basic keyboard engine |
| `BUILD_RIME_ENGINE` | `OFF` | Build the optional Rime engine plugin |
| `ENABLE_WAYLAND` | `ON` | Enable the Wayland frontend |
| `ENABLE_STATUS_BUS` | `ON` | Enable the D-Bus runtime status/control interface |
| `ENABLE_SYSTRAY` | `OFF` | Enable StatusNotifierItem tray support |

Example:

```bash
cmake -S . -B build
cmake --build build
```

Example with Rime enabled:

```bash
cmake -S . -B build -DBUILD_RIME_ENGINE=ON
cmake --build build
```

Use that override only if you want the optional Chinese engine plugin in addition to the built-in `basic` engine.

## Verify the Install

```bash
typio --version
typio --list
```

Expected baseline output includes the built-in `basic` engine even if no external plugins are installed.

When running from the build tree, point Typio at the module directory explicitly:

```bash
./build/src/server/typio --engine-dir ./build/engines --list
```
