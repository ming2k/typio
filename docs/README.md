# Typio Documentation

Typio is a Wayland-native input method stack built around a small C core
library and a native `input-method-unstable-v2` frontend. End-to-end text
input depends on the compositor/application `text-input-unstable-v3` path.

## User Guides

- [Installation](user_installation.md)
- [Configuration](user_configuration.md)
- [Usage](user_usage.md)
- [Troubleshooting](user_troubleshooting.md)

## Developer Guides

- [Building from Source](dev_building.md)
- [Architecture](dev_architecture.md)
- [Creating Engines](dev_creating_engines.md)
- [Contributing](contributing.md)

## API Notes

- [Core API](api_core.md)
- [Engine API](api_engine.md)

## What Ships in This Repository

- `typio-core`: instance, engine manager, input context, config, events, utilities
- `typio`: Wayland daemon binary
- `basic` engine: built-in keyboard engine that commits printable Unicode text
- `rime` engine: default `librime`-backed plugin

External engines can also be loaded from the engine directory as shared objects.
