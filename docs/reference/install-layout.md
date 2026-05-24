# Installation Layout Reference

This page describes every file and directory installed by `ninja install` (or `cmake --install`).

Paths are shown relative to the installation prefix (default `/usr/local`). Override with `--prefix` at configure time.

## Binaries

| Path | Type | Description |
|------|------|-------------|
| `bin/typio` | Executable | CLI client. Communicates with the daemon over UDS (or D-Bus fallback). Users run this directly. |
| `bin/typio-control` | Executable | GTK4 control panel (optional, `build_control_panel=true`). Provides a GUI for configuration and engine switching. |
| `libexec/typio/typio-daemon` | Executable | Background daemon. **Not meant to be run directly by users** — start it via systemd, autostart, or the `typio` CLI. |

## Libraries

| Path | Type | Description |
|------|------|-------------|
| `lib/libtypio-core.so` | Shared library | Core library (C ABI / Rust implementation). Loaded by the daemon and by plugin engines at runtime. |
| `lib/libtypio-core.so.4` | Symlink | `SONAME` symlink pointing to `libtypio-core.so.4.0.0`. |
| `lib/libtypio-core.so.4.0.0` | Shared library | Versioned shared library file. |
| `lib/libflux.a` | Static library | flux rendering framework (optional subproject). Linked into the daemon when available. Not installed when flux is missing. |
| `lib/typio/engines/librime.so` | Plugin | Rime engine (optional, `build_rime_engine=true`). Loaded at runtime by the daemon. |
| `lib/typio/engines/libmozc.so` | Plugin | Mozc engine (optional, `build_mozc_engine=true`). Loaded at runtime by the daemon. |

## Headers

| Path | Description |
|------|-------------|
| `include/typio/*.h` | Public C API headers. Required for building third-party engine plugins. |
| `include/flux/*.h` | flux headers (optional). Required for building against flux if the subproject is present. |

## pkg-config

| Path | Description |
|------|-------------|
| `lib/pkgconfig/typio.pc` | pkg-config file for `typio-core`. Defines `Cflags` and `Libs` for engine plugin development. |
| `lib/pkgconfig/flux.pc` | pkg-config file for flux (optional). Present only when flux subproject is resolved. |

## Desktop Integration

| Path | Description |
|------|-------------|
| `share/applications/typio.desktop` | Desktop entry for launching the daemon. `Exec=` points to `libexec/typio/typio-daemon`. |
| `share/applications/typio-control.desktop` | Desktop entry for the GTK4 control panel (optional). |
| `etc/xdg/autostart/typio-autostart.desktop` | Autostart entry. `NoDisplay=true`; starts the daemon on graphical session login. |
| `lib/systemd/user/typio.service` | systemd user unit. Type `simple`; starts and supervises the daemon. |

## Data Files

| Path | Description |
|------|-------------|
| `share/typio/typio.toml.example` | Example configuration file. Copy to `~/.config/typio/typio.toml` and edit. |
| `share/icons/hicolor/scalable/apps/typio-*.svg` | Application and status icons (various sizes and themes). |

## Runtime Directories (not installed)

These directories are created at runtime, not by `ninja install`:

| Path | Description |
|------|-------------|
| `~/.config/typio/` | User configuration directory. Contains `typio.toml`, Rime user data, and engine state. |
| `~/.local/share/typio/` | User data directory. Contains logs, deploy artifacts, and plugin caches. |
| `/run/user/$UID/typio/` | Runtime directory. Contains the UDS socket for CLI-daemon communication. |

## Quick Verification

After installing, verify the layout:

```bash
# Staging install (no root needed)
DESTDIR=/tmp/typio-staging ninja -C build install
find /tmp/typio-staging -type f -o -type l | sort

# Check critical paths
grep ^Exec /tmp/typio-staging/usr/local/share/applications/typio.desktop
grep ^ExecStart /tmp/typio-staging/usr/local/lib/systemd/user/typio.service
cat /tmp/typio-staging/usr/local/lib/pkgconfig/typio.pc

rm -rf /tmp/typio-staging
```
