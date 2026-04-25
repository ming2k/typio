# How to Install Typio

This guide assumes you have already [built Typio from source](../tutorials/01-getting-started.md).

## When to use this

Use this approach when you want a permanent system installation or are preparing a package. If you only want to run from the build tree, the tutorial is sufficient.

## Requirements

- Everything needed to build Typio (see [Getting Started](../tutorials/01-getting-started.md))
- A Wayland compositor that exposes `zwp_input_method_manager_v2`
- Applications using a working `zwp_text_input_manager_v3` path

Optional dependencies:

- `librime` development files if building with `-DBUILD_RIME_ENGINE=ON`
- `dbus-1` development files if building with `-ENABLE_SYSTRAY=ON`

## Build

Default build:

```bash
cmake -S . -B build
cmake --build build
```

With optional Rime engine:

```bash
cmake -S . -B build -DBUILD_RIME_ENGINE=ON
cmake --build build
```

## Useful build options

| Option | Default | Purpose |
|--------|---------|---------|
| `BUILD_SHARED_LIBS` | `ON` | Build `typio-core` as a shared library |
| `BUILD_TESTS` | `ON` | Build the automated tests |
| `BUILD_BASIC_ENGINE` | `ON` | Build the built-in basic keyboard engine |
| `BUILD_RIME_ENGINE` | `OFF` | Build the optional Rime engine plugin |
| `BUILD_CONTROL_PANEL` | `OFF` | Build the `typio-control` GTK4 control panel |
| `ENABLE_WAYLAND` | `ON` | Enable the Wayland frontend |
| `ENABLE_STATUS_BUS` | `ON` | Enable the D-Bus runtime status/control interface |
| `ENABLE_SYSTRAY` | `OFF` | Enable StatusNotifierItem tray support |
| `ENABLE_ASAN` | `OFF` | Enable AddressSanitizer |
| `ENABLE_UBSAN` | `OFF` | Enable UndefinedBehaviorSanitizer |

## Install

System-wide install:

```bash
sudo cmake --install build --prefix /usr/local
```

Staging install (disposable, no root):

```bash
cmake --install build --prefix /tmp/typio-staging
/tmp/typio-staging/bin/typio --version
rm -rf /tmp/typio-staging
```

Installed paths (with `/usr/local` prefix):

- `/usr/local/bin/typio`
- `/usr/local/lib/libtypio-core.so`
- `/usr/local/lib/typio/engines/rime.so` (if built)
- `/usr/local/include/typio/*.h`
- `/usr/local/lib/pkgconfig/typio.pc`
- `/usr/local/share/typio/typio.toml.example`
- `/usr/local/share/applications/typio.desktop`
- `/etc/xdg/autostart/typio.desktop`

Override the autostart location with `-DTYPIO_AUTOSTART_DIR=/some/path` if needed.

## Verification

```bash
typio --version
typio --list
```

Expected baseline output includes the built-in `basic` engine even if no external plugins are installed.

## Common issues

- **`typio --list` shows no external engines**: Check the engine directory (`/usr/local/lib/typio/engines`) and ensure plugins were built and installed.
- See [Troubleshooting](troubleshooting.md) for runtime problems.
