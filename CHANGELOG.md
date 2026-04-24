# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [3.0.3] - 2026-04-24

### Fixed

- **Rendering pipeline stability**: Replaced per-frame Vulkan offscreen surface
  creation with a persistent surface stored in `PopupRenderCtx`.  This eliminates
  per-frame Graphics Pipeline reconstruction and the associated
  `vkDeviceWaitIdle` / `vkQueueWaitIdle` storms that previously caused 5-second
  GPU stalls, system-wide卡顿, and watchdog-forced process termination.
- **Font fallback for missing glyphs**: `flux_renderer.c` now caches fontconfig
  results and `fx_font` objects, and implements a fallback path via `FcCharSet`
  when the primary font lacks glyphs for the input text.  This fixes CJK and
  symbol characters rendering as tofu blocks.
- **Text gamma correction**: Added `pow(alpha, 1.0/2.2)` to the Flux text
  fragment shader so glyph coverage blends perceptually rather than in linear
  space, correcting text that appeared too thin.
- **Documentation**: Updated `docs/architecture.md` to reflect that all popup
  paint paths now perform full redraws over the persistent Vulkan surface.

## [3.0.2] - 2026-04-18

### Fixed

- **Rime engine sync performance**: Fixed a bug in the selection-only fast path
  where the engine would fall back to an expensive full-sync even when the
  content was unchanged. This reduces CPU usage and UI latency during candidate
  navigation for schemes with static comments.
- **Wayland popup delta classification**: Refined internal logic for detecting
  content changes to better support future incremental rendering optimizations.

## [3.0.1] - 2026-04-15

### Fixed

- **Candidate popup text rendering on 1080p**: Text positions are now stored and
  passed as floats throughout the layout and paint pipeline, preserving subpixel
  glyph placement that was previously lost to integer truncation.
- **Candidate popup colour compositing**: Removed the old off-screen blend path
  used to recolour paragraph glyphs at paint time. Text colour is now baked into
  layout creation, eliminating an unnecessary compositing pass.
- **LRU layout cache**: Cache identity now includes the text colour so that
  unselected and selected colour variants of the same candidate text are stored
  as distinct entries. Cache capacity raised from 64 to 128 entries to
  accommodate the two variants per candidate row.

## [3.0.0] - 2026-04-15

### Added

- **Unified CLI**: `typio daemon` and `typio client` modes consolidated into a
  single binary with a shared command-line interface.
- **Flux rendering backend**: Replaced Cairo/Pango with Flux for candidate popup
  painting, enabling Vulkan-backed rendering and richer typography.
- **Modernized candidate UI**: Nested rounded corners, improved baseline
  alignment, and refined typography for the candidate popup.

### Changed

- **Project structure**: Consolidated app components into `src/apps/typio`.

### Fixed

- **Build system hardening**: ASan/UBSan enabled for development builds.

## [2.9.0] - 2026-04-12

### Added

- **Flux backend**: Added experimental Flux rendering backend as an alternative
  to Cairo for candidate popup painting, enabling Vulkan-backed rendering.

## [2.8.0] - 2026-04-11

### Added

- **Arena allocator**: Introduced a fast arena-based memory allocator for 
  transient core objects.

### Changed

- **Refined candidate UI**:
  - Candidate index labels are now automatically scaled to 80% of the candidate 
    font size for better visual hierarchy.
  - Improved vertical layout with row height normalization for a more 
    consistent appearance.
- **Wayland frontend refactoring**: Internal focus transition logic has been 
  decoupled into explicit state machine handlers, improving maintainability 
  and event tracing.

### Fixed

- **Rime hot-reload after deploy**: `typio-client rime deploy` now takes
  effect immediately without restarting Typio. After maintenance completes,
  a `deploy_id` increment triggers transparent session recreation on next use,
  ensuring all existing contexts pick up the newly compiled Rime data.

## [2.7.3] - 2026-04-10

### Added

- **Popup palette overrides**: candidate popup light and dark themes now
  support per-channel color overrides from `typio.toml`, including background,
  border, text, muted text, preedit, selection, and selection-text colors.

### Changed

- **Candidate popup row layout**: candidate indices are rendered as a separate
  muted label column with tighter horizontal spacing and refined padding for a
  denser popup layout.
- **Popup font configuration**: candidate and auxiliary popup text now reuse a
  configured font family consistently across cached layouts and theme rebuilds.

### Fixed

- **Candidate row vertical alignment**: candidate indices and primary text now
  align against their visible ink extents instead of sharing one text offset,
  avoiding mismatched vertical centering between numbers and glyphs.
- **Selection highlight density**: selected candidates now keep tighter outer
  spacing while preserving enough internal vertical padding for the highlighted
  content to read cleanly.

## [2.7.2] - 2026-04-10

### Changed

- **Async voice engine reloads**: Whisper and Sherpa-ONNX config reloads now
  load replacement models on a background thread and hot-swap them into a
  proxy backend, avoiding synchronous model teardown and reload stalls on the
  main event loop.

### Fixed

- **Rime deploy rebuild reliability**: explicit Rime deployment now invalidates
  generated `build/*.yaml` artifacts before maintenance so rapid successive
  edits to `default.custom.yaml` within the same second still rebuild the
  compiled config.

## [2.7.1] - 2026-04-09

### Fixed

- **Candidate popup progressive lag**: Fixed a Pango/Cairo font options
  mismatch that caused `pango_cairo_update_layout()` to re-shape text on
  every paint call, defeating the LRU layout cache and degrading
  performance over time.
- **Unnecessary cache invalidation**: The popup layout cache is no longer
  cleared on every hide-to-show transition; only a content rebuild is
  triggered when no prior geometry exists.

## [2.7.0] - 2026-04-07

### Removed

- **Obsolete popup components**: Removed older Wayland candidate popup abstraction files and tests (`candidate_popup_damage`, `candidate_popup_state`, `candidate_popup_render_state`) rendered obsolete by the recent redesign.

## [2.6.4] - 2026-04-06

### Fixed

- **Rime hot-reload after deploy**: `typio-client rime deploy` now takes
  effect immediately without restarting Typio. After maintenance completes,
  `cleanup_stale_sessions()` invalidates all existing librime sessions so
  they are transparently recreated with the newly compiled data on next use.

## [2.6.3] - 2026-04-06

### Fixed

- **Virtual keyboard fail-safe false positive**: the drop counter now resets
  on each successful keymap receipt instead of accumulating across the entire
  session, preventing a spurious fail-safe stop after many focus-change cycles.
  Lifecycle hard-reset no longer counts against the drop limit, and stuck-VK
  detection is now solely owned by the 1500 ms keymap deadline.

## [2.6.2] - 2026-04-03

### Added

- **Explicit Rime deployment action**: added a `DeployRimeConfig` D-Bus
  method, `typio-client rime deploy`, a tray submenu action, and a control
  panel Rime settings button so user edits under the Rime data directory can
  manually regenerate `build/*.yaml` without a persistent `full_check`
  config flag.

### Changed

- **Recent log snapshot de-duplication**: recent-log dumps now persist only
  `state_dir/logs/latest.log` and no longer write a second archive copy for the
  same snapshot.
- **Rime CLI namespacing**: moved user-facing Rime commands under
  `typio-client rime ...` while keeping the legacy top-level `schema`
  command as a compatibility alias.

### Fixed

- **Legacy recent-log cleanup**: daemon startup now removes stale
  top-level `state_dir/typio-recent*.log` files from the pre-2.5.1 layout while
  keeping the current `state_dir/logs/latest.log` snapshot and runtime state
  files intact.
- **Control panel engine list refresh**: the keyboard-engine settings UI now
  builds its available-engine model from `AvailableKeyboardEngines` instead of
  `OrderedKeyboardEngines`, so a stale custom order containing only `basic`
  no longer hides other engines until Reset is clicked.
- **Rime deploy action scope**: manual Rime deployment now resolves the Rime
  engine by name and initializes it on demand instead of requiring `rime` to
  be the currently active keyboard engine.

## [2.6.1] - 2026-04-01

### Fixed

- **Virtual-keyboard keymap fd leak**: close the duplicated compositor keymap
  descriptor after forwarding it to the virtual keyboard, preventing gradual
  file-descriptor exhaustion that could make candidate navigation sluggish
  during long daemon runtimes.

## [2.6.0] - 2026-04-01

### Added

- **Per-application keyboard preferences**: Typio can now remember and restore
  both the active keyboard engine and the engine-specific mode for each
  focused-application identity, with runtime support for restoring Rime and
  Mozc sub-modes.
- **Keyboard preference toggle in control panel**: added a `Per-app
  preferences` switch to the `Keyboard engines` page so per-application engine
  and mode restore can be enabled or disabled without editing config files.
- **Basic engine printable-key routing setting**: the built-in `basic` engine
  now exposes a `Printable keys` control that hot-switches between forwarding
  real key events and committing text directly.

### Changed

- **Basic engine passthrough routing**: printable keys routed through `basic`
  now use an explicit passthrough tracking path, keeping symmetric
  press/release forwarding without changing external engine routing semantics.
- **Identity state storage**: focused-application state now stores structured
  `engine`, `mode_engine`, and `mode_id` entries while still reading older
  engine-only mappings for compatibility.

### Fixed

- **Rime Shift mode toggle regression**: restored the release path so forwarded
  modifier keys still reach engine release handling, allowing `Shift` to toggle
  Rime mode again.
- **Control panel engine-order filtering**: stale non-keyboard entries such as
  debug-only engines are now filtered out of the `Keyboard engines` ordering UI.
- **Control widget warning cleanup**: removed the `-Wsign-conversion` warning in
  control-panel slug generation.

## [2.5.2] - 2026-04-01

### Changed

- **Wayland observability layering**: clarified log-level ownership across the
  lifecycle, virtual-keyboard, keyboard, and status-export paths, and exposed
  keyboard-generation diagnostics through `RuntimeState`.

### Fixed

- **Virtual-keyboard keymap generation tracking**: vk readiness now requires a
  keymap from the current keyboard-grab generation instead of reusing stale
  readiness from an earlier grab.
- **Deactivation teardown timing**: `deactivate` now defers keyboard cleanup to
  the `done` boundary, preventing premature grab teardown while reactivation is
  still settling.

## [2.5.1] - 2026-04-01

### Changed

- **Recent log snapshot layout**: moved Typio's persisted recent dumps under
  `~/.local/state/typio/logs/`, with a fixed `latest.log` entrypoint for the
  most recent persisted snapshot.
- **Recent log snapshot naming**: persisted dumps now use the stable
  `latest.log` path instead of creating timestamped archive filenames or
  encoding the dump reason in the filename.
- **Wayland key-routing model**: internal routing now expresses a final
  `action` (`consume` or `forward`) separately from `reason`, replacing the
  older route-class abstraction and making trace output easier to interpret.
- **Per-key tracking naming**: renamed tracking-only states to explicit
  `TYPIO_KEY_TRACK_*` names so lifecycle ownership is no longer conflated with
  routing decisions.

### Fixed

- **Stale non-routable keyboard grabs**: added recovery when the input-method
  lifecycle completes without a usable routed state, reducing cases where
  Typio kept a grab active while ordinary key events were rejected.
- **Guard-reject freeze recovery**: repeated routing-guard rejections now log
  an explicit recovery reason, dump a recent snapshot, and stop the frontend
  instead of staying stuck with an unusable keyboard path.

## [2.5.0] - 2026-03-31

### Added

- **Source-tree architecture docs**: documented the new `src/core`,
  `src/apps`, and `src/engines` layout across the main README and developer
  documentation so build-tree paths, source ownership, and control-surface
  boundaries are described consistently.

### Changed

- **Source tree reorganization**: regrouped the codebase around stable product
  boundaries, moving shared library code to `src/core/`, executable entrypoints
  to `src/apps/`, and keeping engine implementations under `src/engines/`.
- **Core library split**: clarified the internal `typio-core` boundary by
  separating installed public headers (`src/core/include/typio/`), runtime
  implementation (`src/core/runtime/`), and internal support code
  (`src/core/utils/`).
- **Daemon naming cleanup**: renamed daemon-local app/CLI units from the old
  `server_*` terminology to `daemon_*` naming so file names, types, functions,
  and build structure use one vocabulary.
- **Build graph cleanup**: updated CMake and tests to match the new source
  layout, including explicit Wayland protocol generation dependencies for
  daemon-adjacent test targets.

## [2.4.1] - 2026-03-30

### Changed

- **Wayland text UI backend boundary**: introduced an internal
  `text_ui_backend` layer between `wl_input_method` and the concrete
  candidate popup implementation so UI presentation can evolve without
  coupling style/layout work to input-method protocol code.
- **Wayland frontend convergence**: extracted event-loop and runtime-config
  responsibilities out of `wl_frontend.c` into dedicated modules, reducing
  the amount of lifecycle, polling, and reload logic concentrated in one file.

### Fixed

- **Build warning cleanup**: removed release-build warnings in the Mozc
  candidate list path, server log timestamp formatting, and `test_server_app`
  systray macro definitions.

## [2.4.0] - 2026-03-30

### Added

- **Engine mode abstraction**: new `TypioModeClass` (NATIVE/LATIN) and
  `TypioEngineMode` struct give the framework a unified way to observe
  engine sub-modes without taking over mode switching.
- **Per-mode icons**: every engine × mode combination now has its own SVG
  icon. Rime uses an "R" identity with an amber dot for Latin mode; Mozc
  differentiates Hiragana, Katakana, Half-Katakana, Direct, Half-ASCII,
  and Full-ASCII with colour-coded corner indicators.
- **D-Bus `ActiveEngineMode` property**: exposes mode_class, mode_id,
  display_label, and icon_name as an `a{sv}` dict on the status interface.
- **Candidate popup mode indicator**: displays "Engine Mode" label
  (e.g. "Rime 中", "Mozc あ") — bottom-right with divider in vertical
  layout, inline right-aligned in horizontal layout.
- **`display.popup_mode_indicator` config toggle**: control panel switch
  under Appearance to show or hide the popup mode indicator, persisted
  to `typio.toml`.
- **Cairo pixmap fallback indicators**: amber indicators for Latin modes
  (dot for half-width, square for full-width) and blue indicators for
  Katakana modes (filled dot for full-width, ring for half-width).

### Changed

- **Rime icon redesign**: replaced "中" character with "R" letter across
  all Rime SVG icons and the Cairo pixmap fallback, giving Rime a
  distinct visual identity consistent with Mozc's "M".
- **Engine `get_mode` op**: engines now implement `get_mode` returning
  `const TypioEngineMode *` instead of `get_status_icon`. The framework
  falls back to `get_status_icon` for engines that have not migrated.

## [2.3.2] - 2026-03-29

### Added

- **`typio-client` CLI**: new D-Bus client binary for controlling a running
  Typio daemon from the command line, with subcommands for engine switching,
  Rime schema selection, config management, status display, and daemon stop.
  Built automatically when `ENABLE_STATUS_BUS=ON`.
- **`NextEngine` D-Bus method**: the `org.typio.InputMethod1` interface now
  exposes a `NextEngine()` method so external tools can cycle engines without
  knowing engine names.
- **D-Bus interface reference**: added `docs/reference/dbus-interface.md`
  documenting the full property and method surface.

### Changed

- **Fast/slow engine switching**: Ctrl+Shift rapid presses cycle through the
  ordered keyboard engine list (fast switch); presses after a 1 s idle interval
  toggle between the two most recently committed engines (slow switch).  The
  recent pair is now tracked by commit events instead of switch timing and
  persists across daemon restarts in `engine-state.toml`.
- **Status icon clearing on switch**: stale status icons from the previous
  engine are now cleared before the new engine's focused-context rebind,
  preventing icon flicker when switching away from engines that refresh their
  icon during `focus_out`.

### Removed

- Dead engine-switch bookkeeping: `prev_active_keyboard_index`,
  `engine_manager_note_stable_current`, and `engine_manager_has_explicit_order`
  have been removed in favour of the commit-based recent-pair model.

## [2.3.1] - 2026-03-29

### Fixed

- **Tray icon not updating on engine switch**: switching engines via
  Ctrl+Shift (e.g. Rime → Basic) left the tray showing the previous
  engine's icon because Rime's `focus_out` re-set the status icon cache
  after it was cleared.

### Changed

- **Rime tray icon**: replaced the red "R" badge and hardcoded trident
  symbol with a minimal `currentColor` stroke glyph that adapts to both
  light and dark desktop themes.

## [2.3.0] - 2026-03-28

### Changed

- **Candidate popup modularization**: split the Wayland candidate popup path
  into dedicated `candidate_popup` modules for coordination, shm buffer
  pooling, layout/cache management, Cairo painting, theme resolution, and
  focused state helpers so rendering behavior is easier to trace and maintain.
- **Internal naming consistency**: renamed the popup subsystem, tests, and
  developer documentation from generic `popup` internals to
  `candidate_popup`, aligning implementation structure with the feature's
  actual responsibility.

## [2.2.7] - 2026-03-26

### Changed

- **Engine category model**: Typio now treats keyboard and voice engines as
  explicit parallel runtime categories instead of one flat engine set, with
  independent active-engine slots and clearer status-bus exports for each.
- **Control-surface semantics**: the control panel, status surface, and public
  API/docs now consistently describe `basic` as the baseline keyboard engine
  and voice backends as a separate non-conflicting category.
- **Tray runtime presentation**: tray state now keeps the keyboard engine as
  the icon source of truth while surfacing voice-backend state through tooltip
  text and runtime properties instead of conflating the two roles.
- **Candidate popup architecture docs**: developer and user documentation now
  describe the popup pipeline as a layered UI path where the keyboard engine
  owns candidate state and the Wayland popup remains a rendering surface, not
  the source of truth.

## [2.2.6] - 2026-03-26

### Changed

- **Wayland watchdog diagnostics**: watchdog timeouts now report the last
  observed event-loop stage, stage duration, lifecycle phase, virtual-keyboard
  state, and pending-popup status before forcing process exit, making hard
  hangs during Rime/popup interaction substantially easier to attribute.

### Fixed

- **Popup snapshot ownership**: fixed a use-after-free/double-free bug in the
  popup snapshot cache path that could abort Typio immediately after rendering
  a candidate list under Rime.
- **Candidate highlight synchronization**: fixed the selection-only popup
  redraw path so cached snapshots survive full renders correctly and popup
  cache selection state is only advanced after a successful redraw, preventing
  Up/Down navigation from leaving the visible highlight stuck on the old item.
- **Candidate popup flicker on Up/Down navigation**: popup redraw failures now
  keep the previous visible popup frame instead of hiding the candidate window,
  and the popup shm buffer pool has been expanded to reduce transient
  buffer-exhaustion during rapid selection changes.

## [2.2.5] - 2026-03-26

### Added

- **Build metadata display**: the CLI and startup path now expose a unified
  build identifier in the form `Typio x.y.z (<source-label>)`, using git
  describe output when available and falling back to `source` for non-git
  source builds.
- **Popup state regression coverage**: added dedicated tests for popup damage
  calculation, popup invalidation policy, popup render-cache reuse decisions,
  text-UI deferred-update state, and Rime selection-only candidate navigation.

### Changed

- **Candidate navigation hot path**: selection-only candidate movement now
  avoids redundant preedit rebuilding, defers popup work out of the current key
  handling stack, and reuses cached popup row bitmaps for minimal redraw.
- **Popup cache organization**: popup cache invalidation, output-change policy,
  and render-cache matching rules are now modeled through smaller helper units
  so behavior is easier to reason about and harder to regress silently.
- **Wayland text-UI state management**: deferred popup updates now require a
  still-focused context before being flushed, and focus-out/reset/commit all
  clear pending popup work consistently.

## [2.2.4] - 2026-03-25

### Changed

- **Startup method**: removed the manual-launch
  `share/applications/typio.desktop` entry and the systemd `--user` service
  example from documentation; XDG autostart is now the single supported
  launch method.
- **Popup candidate rendering**: candidate selection changes (Up/Down keys)
  now skip full candidate-list reallocation and Pango text re-measurement,
  using a lightweight selection-only update path through the Rime engine and
  a render cache that owns line data directly.
- **Popup resource caching**: `PangoFontDescription` objects and desktop
  theme detection results are cached across frames instead of being
  re-parsed or re-read from disk on every draw.
- **Stderr log timestamps**: log lines now include a wall-clock timestamp so
  captures via `tee` or compositor log streams are self-contained.

## [2.2.3] - 2026-03-25

### Fixed

- **Wayland virtual-keyboard timeout teardown**: fixed a stale keymap-wait
  state after rapid keyboard-grab teardown and rebuild churn, so Typio no
  longer triggers its own fail-safe stop while no active grab remains to
  deliver the pending keymap.

## [2.2.2] - 2026-03-24

### Added

- **Wayland runtime state export**: the status D-Bus surface now exposes a
  structured `RuntimeState` snapshot for the Wayland frontend, covering
  lifecycle phase, keyboard-grab activity, virtual-keyboard health, watchdog
  state, and key timing ages so runtime failures can be diagnosed without
  reconstructing state from text logs alone.
- **Runtime-state change notifications**: virtual-keyboard state changes,
  keymap arrival, and keyboard-grab create/release events now emit
  `PropertiesChanged`, making `gdbus monitor` and other status-bus clients
  useful for live diagnosis.

### Changed

- **Wayland lifecycle cleanup**: reduced duplicate watchdog, shortcut logging,
  virtual-keyboard progress, and frontend init-failure logic so the most
  failure-sensitive Wayland paths are easier to reason about and maintain.

### Fixed

- **Status-bus property completeness**: `EngineDisplayNames` is now included in
  `GetAll`, introspection output, and property-change emission.
- **D-Bus string-map encoding**: fixed the `a{ss}` status-bus dictionary
  encoding path so `EngineDisplayNames` and related consumers no longer rely on
  invalid variant-wrapped payloads.

## [2.2.1] - 2026-03-24

### Added

- **Candidate slow-path diagnostics**: added low-noise verbose timing logs for
  Rime context sync, Wayland text-UI updates, and popup rendering so
  long-running candidate-list stalls can be attributed to the correct layer.

### Changed

- **Candidate popup redraw fast path**: candidate-list updates now carry a
  stable content signature so selection-only movement can reuse popup layout
  and rendering caches without reformatting and re-comparing every candidate
  row on each highlight change.

## [2.2.0] - 2026-03-23

### Added

- **Tray icon regression coverage**: added dedicated server-app tests to lock
  down tray icon behavior across engine switches, including Rime `ascii_mode`
  transitions and fallback to static icons for engines like `basic`.

### Changed

- **Rime schema persistence**: moved the selected Rime schema out of
  `typio.toml` and into XDG state so runtime schema changes are persisted
  without treating them as stable user config.
- **Engine-owned paging behavior**: removed `page_size` from Typio config for
  both Rime and Mozc so candidate paging always follows the underlying engine's
  own configuration and runtime responses.
- **Canonical config cleanup**: dropped legacy voice/config compatibility paths
  so Typio now only accepts the current canonical configuration layout.

### Fixed

- **Tray icon synchronization**: fixed engine-switch timing and tray refresh
  logic so Rime status icons stay in sync with the actual input mode and static
  engines no longer inherit stale Rime icons.
- **Popup config lifetime**: fixed a borrowed-config free in the Wayland popup
  path that could crash Typio after candidate rendering.

## [2.1.5] - 2026-03-21

### Added

- **Focused-app identity tracking**: introduced a dedicated focused-app
  identity layer for the Wayland frontend, including initial `niri` support
  and regression coverage for the new abstraction.

### Changed

- **Virtual keyboard observability and shortcut routing**: improved key-route
  and virtual-keyboard diagnostics while tightening shortcut routing behavior
  through the Wayland frontend.
- **Repository cleanup**: removed the committed `build-headless/` output tree
  so release history no longer carries generated binaries and protocol output.

## [2.1.4] - 2026-03-19

### Changed

- **Rime session lifecycle**: Rime sessions are now treated as input-context
  state instead of transient focus state, so runtime options such as
  `ascii_mode` survive focus churn and engine switches within the same
  context while composition UI is still cleared on focus loss.

## [2.1.3] - 2026-03-19

### Added

- **Desktop launcher for Typio**: installed a visible
  `share/applications/typio.desktop` entry alongside the existing autostart
  entry, so desktop launchers such as `drun` can start Typio manually.

## [2.1.2] - 2026-03-19

### Added

- **Emergency exit shortcut**: Typio now reserves a configurable
  `shortcuts.emergency_exit` action, defaulting to `Ctrl+Shift+Escape`, to stop
  the frontend immediately and release the keyboard grab when input routing
  goes wrong. The control panel exposes the shortcut alongside the existing
  engine-switch and voice PTT bindings.
- **Diagnostics helpers**: added `scripts/run_asan.sh` for ASAN/LSAN test runs
  and `scripts/capture_typio_trace.sh` for capturing verbose runtime traces
  during freeze or routing failures.
- **Auxiliary event-loop policy tests**: introduced dedicated regression
  coverage for tray/status/voice auxiliary FD degradation handling and recent
  log-dump support.

### Changed

- **Candidate navigation ownership**: when a candidate list is active, arrow
  keys are now reserved for candidate movement instead of being forwarded
  through to the focused application, avoiding conflicts with in-app dropdown
  widgets.
- **Auxiliary FD degradation**: tray, status bus, and voice completion sources
  now degrade independently when their poll or dispatch path fails instead of
  risking a broader Typio event-loop stall.
- **Runtime observability**: Typio now keeps a ring buffer of recent log lines
  and dumps them to `state_dir/typio-recent.log` on emergency exit and other
  controlled shutdown paths, preserving the last visible activity before a
  failure.

## [2.1.1] - 2026-03-17

### Fixed

- **Chrome candidate navigation lag**: skip redundant `zwp_input_method_v2`
  protocol commits when only the candidate highlight changes (e.g. Up/Down
  arrow navigation), avoiding expensive composition-update round-trips in
  heavyweight clients like Chromium.

## [2.1.0] - 2026-03-17

### Added

- **Keyboard engine management UI**: the control panel now treats keyboard
  engines as a first-class managed list, with drag reordering, active-engine
  actions, per-engine editing, and persistent `engine_order` handling.
- **Selector contract tests**: added broader control-panel regression coverage
  for config-driven and runtime-driven selectors so mismatches between
  `options -> UI`, `config/runtime -> UI`, and `UI -> config` are caught by
  tests instead of slipping into runtime.

### Changed

- **Control-panel engine editing UX**: engine-specific settings now open in a
  dedicated GTK window instead of replacing content inline, and the keyboard
  engine area uses clearer active-state labeling and a more predictable layout.
- **Single-source engine naming**: keyboard engine labels shown by the tray and
  control panel now follow daemon-exported engine display metadata instead of
  drifting across surfaces.
- **Engine ordering semantics**: keyboard engine display/switch order is now
  explicitly modeled through `engine_order`, `EngineOrder`, and
  `OrderedEngines`, giving tray, switching, and the control panel the same
  ordering source.

### Fixed

- **Atomic config saves**: daemon-side config writes now use temporary files
  plus atomic replace, preventing a crash during save from truncating the whole
  `typio.toml`.
- **Control-panel startup sync**: opening the control panel no longer seeds an
  empty local buffer over the real daemon config, which previously could erase
  live settings or show stale selector state.
- **Rime schema persistence and display**: `engines.rime.schema` now roundtrips
  cleanly between disk, daemon, and control panel, supports an explicit
  unselected state, and preserves configured values even when schema discovery
  lags.
- **Voice backend/model population**: the voice section now sources backend
  choices from `AvailableEngines` rather than keyboard-only ordered engines, so
  Whisper and sherpa-onnx appear correctly and installed models can be chosen.

## [2.0.1] - 2026-03-16

### Fixed

- **Embedded control-panel CSS**: the GTK control panel now compiles
  `typio-control.css` directly into the `typio-control` binary via GResource,
  so installed builds no longer lose styling when the external CSS file is not
  found at runtime.

## [2.0.0] - 2026-03-16

### Changed

- **Control-surface documentation refresh**: updated the control-panel
  documentation to match the current GTK layout, native styling direction, and
  instant-apply autosave behavior. The old `Apply` / `Cancel` seeded-draft
  model is no longer documented as current behavior.

## [1.5.0] - 2026-03-16

### Added

- **Configurable keyboard shortcuts**: shortcuts for engine switching and voice
  PTT are now defined in `[shortcuts]` config section instead of being hardcoded.
  Supports modifier-only chords (e.g. `Ctrl+Shift`) and modifier+key combos
  (e.g. `Super+v`).
- **Control center redesign**: reorganized into tabbed layout with Display,
  Engine, Voice, Shortcuts, and Models tabs.  Display settings (popup theme,
  candidate layout, font size) take effect in real time.  Engine configuration
  is contextual to the currently selected engine.
- **Shortcut recorder**: click-to-record shortcut capture in the Shortcuts tab,
  supporting both modifier-only and modifier+key bindings.
- **Voice model dropdown**: voice model is now selected from a dropdown that
  scans installed models on disk, replacing the previous text entry.
- **Sherpa-ONNX model auto-detection**: when no model is specified, the sherpa
  backend scans its model directory for the first usable model instead of
  requiring a `default/` directory.
- **Voice service runtime reload**: changing voice backend or model in the
  control center takes effect immediately without restarting the server.

### Fixed

- **Config serializer duplicate sections**: `typio_config_to_string` now groups
  entries by section, preventing duplicate `[voice]`, `[engines]` etc. headers
  that caused config corruption on save.
- **Voice backend build flag**: added missing `typio_build_config.h` include in
  `voice_service.c` so `HAVE_SHERPA_ONNX` / `HAVE_WHISPER` are properly defined.
- **Settings not persisting**: voice model, backend, and shortcut changes in the
  control center now save immediately instead of only syncing to an in-memory
  buffer.

## [1.4.2] - 2026-03-15

### Added

- **Key event arbiter**: introduced a buffering arbiter layer between raw
  keyboard events and engine dispatch so that system shortcut sequences
  (Ctrl+Shift chord) are fully resolved before any modifier events reach the
  active engine.  This prevents Rime from misinterpreting Shift release as a
  Chinese/English toggle during engine switching.
- **MRU engine switching**: engine cycling now returns to the previously used
  engine when the switch interval exceeds 1.5 s, matching the common two-engine
  toggle workflow.  Rapid successive switches still cycle sequentially.

### Changed

- **Physical modifier update reorder**: `kb_handle_key` now updates physical
  modifier state before dispatching through the arbiter so routing decisions
  have accurate modifier information.
- **Chord state consolidation**: replaced the three scattered
  `shortcut_chord_armed/saw_non_modifier/switch_triggered` bools with the
  arbiter state machine, removing chord-firing logic from both `key_route.c`
  and `wl_keyboard.c`.

## [1.4.1] - 2026-03-15

### Added

- **Unified TOML example**: added a single `typio.toml.example` that documents
  the supported root configuration layout for Rime, Mozc, and Whisper.

### Changed

- **Single-source configuration model**: standardized user configuration on
  `typio.toml` as the only supported format and removed legacy `.conf`
  compatibility paths from runtime loading and tests.
- **Root-owned runtime config**: built-in integrations now read their settings
  from the live root configuration model instead of relying on separate
  per-engine config files.
- **Documentation refresh**: updated user and developer docs to describe the
  TOML-only configuration workflow, D-Bus config editing support, and new
  example/install layout.

## [1.4.0] - 2026-03-15

### Added

- **Mozc engine support**: added a Mozc-backed Japanese input engine with tray
  status icon integration and session-aware mode tracking.

### Changed

- **Mozc IPC compatibility**: aligned Typio's Mozc transport and protobuf subset
  with upstream Mozc, including raw `Input`/`Output` IPC, session creation
  metadata, candidate-window parsing, and abstract Unix socket discovery for
  modern Mozc deployments.
- **Mozc failure handling**: reduced Mozc IPC timeout cost and added retry
  backoff plus richer diagnostics so session-creation failures no longer stall
  every keypress.

## [1.3.0] - 2026-03-15

### Added

- **Rime ascii mode icon**: tray icon switches between `typio-rime` (Chinese)
  and `typio-rime-latin` (English) when Rime toggles `ascii_mode`, via a new
  `get_status_icon` engine op and status icon change callback.

### Changed

- **C23 migration**: bumped C standard from C11 to C23 and CMake minimum from
  3.16 to 3.20. Replaced `NULL` with `nullptr`, removed `<stdbool.h>` includes,
  adopted `{}` empty initializers, converted `(void)param` casts to
  `[[maybe_unused]]` parameter attributes, and centralized `_POSIX_C_SOURCE`
  in CMakeLists.txt.
- **Tray menu**: removed the redundant "Current: xxx" header from the
  right-click menu since the engine list already marks the active engine.
- **Portable string comparison**: replaced POSIX `strcasecmp` with a hand-rolled
  `tolower()`-based implementation to drop the `<strings.h>` dependency.

### Removed

- Unused `keysym` and `has_composition` parameters from
  `startup_guard_classify_press`.

## [1.2.0] - 2026-03-14

### Added

- **Whisper voice input**: optional speech-to-text support via OpenAI Whisper,
  with PipeWire audio capture (`pw_capture`), a background voice service, and
  a hotkey-triggered dictation mode integrated into the Wayland frontend.

### Changed

- **Preedit format cleanup**: removed the inline candidate fallback path and
  renamed `inline_ui` to `preedit_format`, simplifying the preedit rendering
  pipeline to only the popup and plain preedit-string paths.

## [1.1.2] - 2026-03-14

### Added

- **Autostart desktop entry**: added an XDG autostart launcher and install rule
  so `typio` can be started automatically for user sessions.

### Changed

- **Ctrl+Shift engine switching**: engine cycling now triggers on modifier
  release and tracks input-method serial state globally so the Wayland input
  method path stays in sync across focus and grab transitions.

## [1.1.1] - 2026-03-14

### Added

- **Desktop entry for `typio-control`**: added a minimal
  `typio-control.desktop` launcher and install rule so the GTK4 control panel
  integrates with standard desktop menus.

### Changed

- **Dead-path cleanup**: removed redundant keyboard/tray compatibility code and
  other no-op state that was no longer part of the active runtime model.

## [1.1.0] - 2026-03-14

### Added

- **GTK4 control panel**: added the optional `typio-control` desktop client for
  viewing runtime state, switching engines, and reloading configuration over
  Typio's D-Bus control surface.
- **D-Bus control methods**: the session-bus `org.typio.InputMethod1`
  interface now exposes `ActivateEngine(s)` and `ReloadConfig()` in addition to
  structured runtime properties.

### Changed

- **Status bus decoupled from tray support**: the runtime D-Bus status/control
  interface is now controlled by `ENABLE_STATUS_BUS`, while tray support
  remains separately controlled by `ENABLE_SYSTRAY`.
- **Control-panel build option**: added `BUILD_CONTROL_PANEL` so the GTK4 UI is
  built only when explicitly requested.
- **Tray watcher handling simplified**: watcher ownership tracking now uses a
  single D-Bus match/filter path and removes redundant polling-style fallback
  logic.
- **Lifecycle reactivation rules centralized**: deferred activate and
  reactivation-commit decisions are now expressed through lifecycle helper
  rules and covered by dedicated tests, instead of being implicit in
  `wl_input_method.c` branches.

### Fixed

- **Late tray-host registration recovery**: when `StatusNotifierWatcher` was
  not running at Typio startup and a tray host appeared later, Typio now
  re-registers its StatusNotifierItem from a D-Bus ownership-change filter so
  the tray icon becomes visible without restarting Typio.
- **Activation-boundary stuck Enter cleanup**: when a tray-host startup or
  similar focus churn split an `Enter` press/release pair across keyboard-grab
  generations, Typio now forwards the orphan `Enter` release as boundary
  cleanup instead of consuming it and leaving the application with a stuck
  app-facing `Enter` press.
- **Deferred reactivation during focused sessions**: repeated
  `zwp_input_method_v2.activate` events no longer move the frontend out of the
  `active` phase while the current session is still focused, preventing
  in-flight press/release pairs from being cut off before `done` commits the
  reactivation boundary.
- **Explicit Ctrl+Shift engine switching**: modifier-only `Ctrl+Shift` chords
  are now handled explicitly inside Typio so engine switching remains available
  even while the IME keyboard grab is active.
- **Narrower startup Enter suppression**: startup `Enter` suppression now only
  applies during the short stale-key window of a fresh grab, reducing false
  positives when switching windows and immediately pressing `Enter`.

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
  Rime engine options: `schema`, `popup_theme`, `candidate_layout`,
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

[2.3.2]: https://github.com/user/typio/releases/tag/v2.3.2
[1.2.0]: https://github.com/user/typio/releases/tag/v1.2.0
[1.4.0]: https://github.com/user/typio/releases/tag/v1.4.0
[1.4.1]: https://github.com/user/typio/releases/tag/v1.4.1
[2.0.0]: https://github.com/user/typio/releases/tag/v2.0.0
[2.0.1]: https://github.com/user/typio/releases/tag/v2.0.1
[2.1.0]: https://github.com/user/typio/releases/tag/v2.1.0
[1.5.0]: https://github.com/user/typio/releases/tag/v1.5.0
[1.4.2]: https://github.com/user/typio/releases/tag/v1.4.2
[1.1.2]: https://github.com/user/typio/releases/tag/v1.1.2
[1.1.1]: https://github.com/user/typio/releases/tag/v1.1.1
[1.1.0]: https://github.com/user/typio/releases/tag/v1.1.0
[1.0.3]: https://github.com/user/typio/releases/tag/v1.0.3
[1.0.2]: https://github.com/user/typio/releases/tag/v1.0.2
[1.0.1]: https://github.com/user/typio/releases/tag/v1.0.1
[1.0.0]: https://github.com/user/typio/releases/tag/v1.0.0
