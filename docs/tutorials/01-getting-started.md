# Getting Started: Your First Typio Build

By the end of this tutorial you will have:

- A successful local build of Typio
- A passing test suite
- A running Typio daemon inside your Wayland session
- Verified that the daemon starts and lists engines

**Estimated time:** 15 minutes
**Difficulty:** Beginner

## Prerequisites

- CMake 3.20+
- C11 and C++17 compiler
- Rust toolchain (latest stable `rustc` + `cargo`)
- `pkg-config`
- A running Wayland session

### Default build dependencies

These are required for the default build (all builtin options left at their defaults):

| Component | Debian/Ubuntu | Arch Linux | Fedora |
|---|---|---|---|
| Rust toolchain | `rustup` (see [rustup.rs](https://rustup.rs)) | `rust` | `rust` |
| Wayland client | `libwayland-dev` | `wayland` | `wayland-devel` |
| xkbcommon | `libxkbcommon-dev` | `libxkbcommon` | `libxkbcommon-devel` |
| wayland-protocols | `wayland-protocols` | `wayland-protocols` | `wayland-protocols-devel` |
| Vulkan | `libvulkan-dev` | `vulkan-headers` | `vulkan-loader-devel` |
| FreeType | `libfreetype6-dev` | `freetype2` | `freetype-devel` |
| HarfBuzz | `libharfbuzz-dev` | `harfbuzz` | `harfbuzz-devel` |
| fontconfig | `libfontconfig-dev` | `fontconfig` | `fontconfig-devel` |
| D-Bus | `libdbus-1-dev` | `dbus` | `dbus-devel` |
| glslang | `glslang-tools` | `glslang` | `glslang` |

> **Note:** `flux` (the rendering framework) is **not** a system dependency. It is built automatically from source (Meson + Ninja) on the first `cmake --build` run. `meson` and `ninja` must be in your `PATH`.

### Optional dependencies

| Feature | CMake option | Debian/Ubuntu | Arch Linux | Fedora |
|---|---|---|---|---|
| Rime engine | `-DBUILD_RIME_ENGINE=ON` | `librime-dev` | `librime` | `librime-devel` |
| GTK4 control panel | `-DBUILD_CONTROL_PANEL=ON` | `libgtk-4-dev` | `gtk4` | `gtk4-devel` |
| StatusNotifierItem tray | `-DENABLE_SYSTRAY=ON` | `libdbus-1-dev` | `dbus` | `dbus-devel` |
| Voice input (Whisper) | `-DBUILD_WHISPER=ON` | whisper.cpp (see below) | whisper.cpp (see below) | whisper.cpp (see below) |
| Voice input (Sherpa) | `-DBUILD_SHERPA_ONNX=ON` | sherpa-onnx (see below) | sherpa-onnx (see below) | sherpa-onnx (see below) |

Voice backends require **PipeWire**: `libpipewire-0.3-dev` (Debian), `pipewire` (Arch), `pipewire-devel` (Fedora).

`whisper.cpp` and `sherpa-onnx` are not in most distro repositories. See [How to install](install.md) for build instructions.

### One-liner installs

Debian/Ubuntu (default build only):

```bash
# Install Rust first: https://rustup.rs
sudo apt install build-essential cmake pkg-config \
    libwayland-dev libxkbcommon-dev wayland-protocols \
    libvulkan-dev libfreetype6-dev libharfbuzz-dev libfontconfig-dev \
    libdbus-1-dev glslang-tools
```

Arch Linux (default build only):

```bash
# Install Rust first: https://rustup.rs
sudo pacman -S base-devel cmake pkgconf \
    wayland libxkbcommon wayland-protocols \
    vulkan-headers freetype2 harfbuzz fontconfig \
    dbus glslang
```

Fedora (default build only):

```bash
# Install Rust first: https://rustup.rs
sudo dnf install gcc cmake pkgconf-pkg-config \
    wayland-devel libxkbcommon-devel wayland-protocols-devel \
    vulkan-loader-devel freetype-devel harfbuzz-devel fontconfig-devel \
    dbus-devel glslang
```

## Step 1: Clone and configure

```bash
cd /path/to/workspace
git clone <repo-url> typio
cd typio
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

You should see CMake configure output ending with a build summary and no errors.

> **If configuration fails:** Check that `pkg-config` can find `wayland-client` and `xkbcommon`.

## Step 2: Build

```bash
cmake --build build
```

You should see compilation complete and the following binaries appear:

```text
build/daemon/daemon
cli/target/release/typio
```

## Step 3: Run the smoke tests

```bash
ctest --test-dir build --output-on-failure
```

All tests should pass. This verifies your environment is correctly configured.

> **If D-Bus tests fail:** Run with an isolated session bus:
> ```bash
> dbus-run-session -- ctest --test-dir build --output-on-failure
> ```

## Step 4: See it working

Inside your Wayland session, run Typio directly from the build tree:

```bash
./build/daemon/typio-daemon --engine basic --verbose
```

You should see startup logs including:

```text
Typio <version>
...
engine basic activated
```

Press `Ctrl+C` to stop the daemon.

List available engines:

```bash
./build/daemon/typio-daemon --engine-dir ./build/engines --list
```

Expected output includes at least the built-in `basic` engine.

## What's next?

- Want to install Typio permanently? See [How to install](../how-to/install.md)
- Want to configure Typio? See [How to configure](../how-to/configure.md)
- Want to understand the architecture? See [Architecture overview](../explanation/architecture-overview.md)
- Want to contribute code? See [Developer setup](../dev/setup.md)

## Troubleshooting

- **`Failed to connect to Wayland display`**: Make sure `WAYLAND_DISPLAY` is set and `XDG_SESSION_TYPE=wayland`.
- **`Session does not provide the Wayland input-method/text-input protocol stack`**: Your compositor must expose `zwp_input_method_manager_v2`. Verify with `wayland-info | grep zwp_input_method_manager_v2`.
- See [full troubleshooting guide](../how-to/troubleshooting.md) for more.
