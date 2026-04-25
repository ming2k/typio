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
- `pkg-config`
- Wayland client development files
- `xkbcommon` development files
- `wayland-scanner`
- Vulkan, FreeType, HarfBuzz, and fontconfig development files
- `glslangValidator`
- A running Wayland session

### Distro packages

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

Also install your distribution's Vulkan, FreeType, HarfBuzz, fontconfig, and glslang packages.

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
build/src/apps/typio/typio
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
./build/src/apps/typio/typio --engine basic --verbose
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
./build/src/apps/typio/typio --engine-dir ./build/engines --list
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
