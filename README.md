# Typio

Typio is a native Wayland input method daemon. The low-level Wayland frontend and engine plugins remain in C; business logic (config, input context, engine orchestration) is written in Rust for memory safety and maintainability. It runs on the Wayland text-input/input-method protocol stack and provides a daemon frontend, a shared core library, a plugin engine ABI, and a GTK4 control panel.

## Quick Start

```bash
# Configure and build
meson setup build
ninja -C build

# Run tests
meson test -C build --print-errorlogs

# Run directly from the build tree
./build/daemon/typio-daemon --engine basic --verbose
```

Expected output: startup logs ending with `engine basic activated`.

> **Note:** the default build includes only the `basic` engine. Rime, Mozc, the
> voice backends, and the GTK4 control panel are opt-in. Enable what you need at
> configure time, e.g.:
>
> ```bash
> meson setup build \
>   -Dbuild_rime_engine=true \
>   -Dbuild_sherpa_onnx=true \
>   -Dbuild_control_panel=true
> ```
>
> See `meson_options.txt` for the full list of options.

## Documentation

- [Full documentation](docs/index.md)
- [Getting Started Tutorial](docs/tutorials/01-getting-started.md)
- [API Reference](docs/reference/api/)
- [Architecture Overview](docs/explanation/architecture-overview.md)
- [Contributing](CONTRIBUTING.md)

## When to use this project

Typio is a good fit if you need a native Wayland input method with:
- Pure Wayland protocol integration (no X11)
- A plugin engine ABI for custom input engines
- UDS-based JSON-RPC control (primary) with optional D-Bus adapter
- Optional GTK4 control panel

Consider alternatives like Fcitx5 or IBus if you need X11 support or a larger built-in engine ecosystem.

## License

See [LICENSE](LICENSE) for details.
