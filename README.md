# Typio

Typio is a native Wayland input method daemon written in C. It runs on the Wayland text-input/input-method protocol stack and provides a daemon frontend, a shared core library, a plugin engine ABI, and a GTK4 control panel.

## Quick Start

```bash
# Configure and build
cmake -S . -B build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Run directly from the build tree
./build/src/apps/typio/typio --engine basic --verbose
```

Expected output: startup logs ending with `engine basic activated`.

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
- Structured D-Bus runtime control and status
- Optional GTK4 control panel

Consider alternatives like Fcitx5 or IBus if you need X11 support or a larger built-in engine ecosystem.

## License

See [LICENSE](LICENSE) for details.
