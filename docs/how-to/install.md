# How to Install Typio

This guide assumes you have already [built Typio from source](../tutorials/01-getting-started.md).

## When to use this

Use this approach when you want a permanent system installation or are preparing a package. If you only want to run from the build tree, the tutorial is sufficient.

## Requirements

- Everything needed to build Typio (see [Getting Started](../tutorials/01-getting-started.md))
- A Wayland compositor that exposes `zwp_input_method_manager_v2`
- Applications using a working `zwp_text_input_manager_v3` path

### Optional runtime/build dependencies

See the [Getting Started tutorial](../tutorials/01-getting-started.md) for the full package-name table by distro.  Quick reference:

| Feature | Meson option | Packages needed |
|---|---|---|
| Rime engine | `-Dbuild_rime_engine=true` | `librime` dev files |
| GTK4 control panel | `-Dbuild_control_panel=true` | `gtk4` dev files |
| StatusNotifierItem tray | `-Denable_systray=true` | `dbus-1` dev files |
| Voice input | `-Dbuild_whisper=true` or `-Dbuild_sherpa_onnx=true` | PipeWire dev files + voice backend library |

> **Voice backends:** `whisper.cpp` and `sherpa-onnx` are typically not packaged by distributions.  You must build and install them manually before enabling the corresponding Meson option.  See the voice-input explanation doc for details.

## Build

Default build:

```bash
meson setup build
ninja -C build
```

With optional Rime engine:

```bash
meson setup build -Dbuild_rime_engine=true
ninja -C build
```

## Useful build options

| Option | Default | Purpose |
|--------|---------|---------|
| `buildtype` | `debug` | Build type (`plain`, `debug`, `debugoptimized`, `release`, `minsize`) |
| `build_tests` | `true` | Build the automated tests |
| `build_basic_engine` | `true` | Build the built-in basic keyboard engine |
| `build_rime_engine` | `false` | Build the optional Rime engine plugin |
| `build_control_panel` | `false` | Build the `typio-control` GTK4 control panel |
| `enable_wayland` | `true` | Enable the Wayland frontend |
| `enable_status_bus` | `true` | Enable the D-Bus runtime status/control interface |
| `enable_systray` | `false` | Enable StatusNotifierItem tray support |

## Install

System-wide install:

```bash
sudo ninja -C build install
```

Staging install (disposable, no root):

```bash
DESTDIR=/tmp/typio-staging ninja -C build install
find /tmp/typio-staging -type f -o -type l | sort
rm -rf /tmp/typio-staging
```

Installed paths (with default `/usr/local` prefix):

- `/usr/local/bin/typio` — CLI entry point
- `/usr/local/libexec/typio/typio-daemon` — background daemon
- `/usr/local/lib/libtypio-core.so`
- `/usr/local/lib/typio/engines/rime.so` (if built)
- `/usr/local/include/typio/*.h`
- `/usr/local/lib/pkgconfig/typio.pc`
- `/usr/local/share/typio/typio.toml.example`
- `/usr/local/share/applications/typio.desktop`
- `/etc/xdg/autostart/typio.desktop`

Override the prefix at setup time with `--prefix /some/path` if needed.

## Verification

```bash
typio version
typio-daemon --list
```

Expected baseline output includes the built-in `basic` engine even if no external plugins are installed.

## Common issues

- **`typio-daemon --list` shows no external engines**: Check the engine directory (`/usr/local/lib/typio/engines`) and ensure plugins were built and installed.
- See [Troubleshooting](troubleshooting.md) for runtime problems.
