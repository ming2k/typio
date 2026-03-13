# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.3] - 2026-03-14

### Fixed

- **Orphan release forwarding after modifier key-up ordering**: when a shortcut
  key such as `t` becomes an activation-boundary orphan and `Ctrl` is released
  before the non-modifier key, Typio now still forwards the orphan release to
  the virtual keyboard instead of consuming it silently.

## [1.0.2] - 2026-03-14

### Fixed

- **Modifier continuity across deactivation**: when `Ctrl/Alt/Super` stays held
  across IME deactivation, Typio now carries the compositor's last modifier
  mask across the boundary so the newly focused client can still receive the
  intended shortcut, such as `Ctrl+T`, without requiring the modifier to be
  re-pressed.

### Changed

- **Boundary handoff policy extraction**: activation-boundary orphan-release
  cleanup and deactivation modifier carry rules are now centralized in
  `boundary_bridge.*` instead of being split across unrelated helper modules.
- **Developer documentation cleanup**: timing, maintenance, and architecture
  docs now reflect the boundary-bridge model and remove older, superseded
  descriptions of startup-guard ownership and trace workflow details.

## [1.0.1] - 2026-03-14

### Fixed

- **Activation-boundary orphan release cleanup**: when a non-modifier key press
  reached the application before a new keyboard grab was established, Typio now
  forwards the matching startup-window orphan release to the virtual keyboard
  instead of consuming it silently. This prevents stuck shortcut letters such
  as repeating `t` after `Ctrl+T` across focus/grab transitions.
- **Conservative shortcut routing**: Typio no longer intercepts `Ctrl+Shift`
  inside the keyboard grab for engine switching. Modifier shortcuts are treated
  as application/compositor-owned behavior unless exposed through another
  integration surface such as the tray.
- **Modifier continuity across grab recreation**: held `Ctrl/Alt/Super`
  modifiers are now resynchronized from XKB modifier updates after keyboard
  grab recreation, so application shortcuts continue to work without requiring
  the user to release and re-press the modifier.

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
- **Engine switching**: Tray/menu-driven engine switching and schema switching.
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

[1.0.3]: https://github.com/user/typio/releases/tag/v1.0.3
[1.0.2]: https://github.com/user/typio/releases/tag/v1.0.2
[1.0.1]: https://github.com/user/typio/releases/tag/v1.0.1
[1.0.0]: https://github.com/user/typio/releases/tag/v1.0.0
