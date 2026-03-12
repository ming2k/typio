# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-03-13

### Added

- **Core library (`typio-core`)**: Engine manager, input context, configuration,
  key event processing, and shared-library engine ABI.
- **Wayland frontend (`typio`)**: Full `zwp_input_method_v2` implementation with
  keyboard grab, preedit, commit, surrounding text, and content type support.
- **Candidate popup UI**: Pure-Wayland popup surface (`zwp_input_popup_surface_v2`)
  rendered with Cairo/Pango. Supports horizontal and vertical layouts, light/dark
  theme auto-detection, and configurable font size. Falls back to inline preedit
  when popup is unavailable.
- **Virtual keyboard forwarding**: Unhandled keys (Backspace, Ctrl+C, arrow keys,
  etc.) are forwarded to applications via `zwp_virtual_keyboard_v1`.
- **Key repeat**: Client-side key repeat using `timerfd`, respecting compositor
  repeat rate and delay.
- **Rime engine plugin**: Full `librime` integration with schema selection,
  preedit composition, candidate pagination, and Shift-based Chinese/English
  toggle (`ascii_mode`). Both key press and release events are forwarded to
  librime with proper modifier masks.
- **Basic engine**: Built-in passthrough engine that commits printable characters
  directly.
- **Engine switching**: Ctrl+Shift keyboard shortcut to switch engines.
  Interval > 1 s toggles between the two most recent engines; faster intervals
  cycle through all available engines.
- **System tray (StatusNotifierItem)**: D-Bus SNI implementation with engine
  status display, engine switching via click/scroll, and a context menu with
  engine selection. Rime-specific submenu exposes schema switching and reload.
- **Bundled tray icons**: Hicolor icon theme with pre-rendered PNGs at standard
  sizes (16–192 px) for `typio-rime`. Cairo-drawn fallback icons for hosts that
  ignore `IconThemePath`.
- **Engine lifecycle management**: Engines are initialized once and reused across
  focus cycles. Prevents double-init memory leaks.
- **Startup key suppression**: Keys held before the keyboard grab activates are
  suppressed until released, preventing ghost input.
- **Configuration**: INI-style per-engine config files with hot-reload support.
  Rime engine options: `schema`, `page_size`, `popup_theme`, `candidate_layout`,
  `font_size`, `shared_data_dir`, `user_data_dir`.
- **Test suite**: 8 unit/integration tests covering core API, configuration,
  engine manager, inline UI formatting, tray icon pixmap generation, startup
  guard logic, Rime path expansion, and Rime engine lifecycle.
- **Documentation**: User guides (installation, configuration, usage,
  troubleshooting), developer guides (building, architecture, creating engines),
  and API references for core and engine interfaces.
- **CMake build system**: Modular build with optional Wayland, Rime, system tray,
  and test targets. Wayland protocol code generation via custom
  `wayland_generate_protocol()` macro. Supports build-tree testing without
  installation.

[1.0.0]: https://github.com/user/typio/releases/tag/v1.0.0
