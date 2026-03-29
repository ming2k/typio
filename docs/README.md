# Typio Documentation

Typio is a Wayland-native input method stack built around a small C core
library and a native `input-method-unstable-v2` frontend. End-to-end text
input depends on the compositor/application `text-input-unstable-v3` path.

## Layout

The documentation tree is organized by audience first and topic second:

- `docs/user/` for end-user setup and usage
- `docs/dev/` for implementation and contribution guides
- `docs/reference/` for API-oriented reference material

File names use lowercase kebab-case and describe the topic directly. Avoid
prefixing file names with `user_` or `dev_`; the directory already carries
that meaning.

## User Guides

- [Installation](user/installation.md)
- [Configuration](user/configuration.md)
- [Usage](user/usage.md)
- [Troubleshooting](user/troubleshooting.md)
- [Wayland Runtime Diagnostics](user/troubleshooting.md#wayland-runtime-diagnostics)

## Developer Guides

- [Building from Source](dev/building.md)
- [Architecture](dev/architecture.md)
- [Configuration Model](dev/configuration.md)
- [Control Surfaces](dev/control-surfaces.md)
- [Maintenance Manual](dev/maintenance.md)
- [Timing Model](dev/timing-model.md)
- [Creating Engines](dev/creating-engines.md)
- [Contributing](dev/contributing.md)

## Reference

- [Core API](reference/api-core.md)
- [Engine API](reference/api-engine.md)
- [D-Bus Interface](reference/dbus-interface.md)
- [General API Reference](reference/api-reference.md)

## What Ships in This Repository

- `typio-core`: instance, engine manager, input context, config, events, utilities
- `typio`: Wayland daemon binary
- `typio-client`: D-Bus CLI for controlling a running daemon
- `basic` engine: built-in keyboard engine that commits printable Unicode text
- `rime` engine: default `librime`-backed plugin

External engines can also be loaded from the engine directory as shared objects.
