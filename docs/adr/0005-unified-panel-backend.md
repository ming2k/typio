# ADR-0005: Unified panel backend for candidate and status UI

- **Status**: Accepted
- **Date**: 2026-05-22
- **Deciders**: Project maintainers

## Context

Typio currently has one visual floating UI: the candidate popup (`zwp_input_popup_surface_v2` rendered via flux/Vulkan). All other user-facing state is shoe-horned into channels that do not belong to UI:

- Voice recording / processing state is displayed by injecting `[Recording...]` and `[Processing...]` into the preedit string and committing it through the Wayland input-method protocol. This pollutes the application’s input stream, relies on the client’s preedit styling (which may be invisible in some themes), and can interfere with genuine preedit text.
- The `TypioWlTextUiBackend` abstraction is nominally a "text UI backend" but in practice only knows how to render a candidate popup. There is no generic content model that lets other subsystems (voice, tray, future features) contribute visual state to the same surface.

As Typio grows, we expect more floating UI needs: voice waveform visualisation, handwriting pads, quick-phrase palettes, settings panels, etc. Each of these should not acquire its own ad-hoc Wayland surface or protocol channel.

## Decision

Evolve `TypioWlTextUiBackend` into a **unified panel backend** that accepts a generic content model and composites multiple zones inside a single surface.

- **Phase 1** (this ADR): Add a `status` zone to the existing candidate popup surface. Migrate voice recording / processing / error indicators from preedit injection to the status zone. Keep all existing APIs compatible; only add `typio_wl_text_ui_backend_show_status()` / `hide_status()`.
- **Phase 2** (future): Formalise a `TypioPanelContent` content model that aggregates data from `TypioInputContext`, `TypioVoiceService`, and other subsystems. The frontend becomes an aggregator that builds `TypioPanelContent` and pushes it to the panel backend.
- **Phase 3** (future): Split the internal `candidate_popup` into explicit `PanelZone`s (candidates, preedit decor, status, toolbar, etc.) with independent layout and paint modules.
- **Phase 4** (future): If free-floating panels (e.g. a settings window or waveform overlay) are needed, introduce a `LayerShellProvider` as an alternative `SurfaceProvider` without changing the content model or composer.

Key architectural constraints:
- A single `zwp_input_method_v2` can only create one `zwp_input_popup_surface_v2`. Therefore candidate and status UI must share the same surface, rendered as distinct zones by the composer.
- The content model (`TypioPanelContent`) must remain free of Wayland or GPU types so it can be unit-tested without a display server.
- Layout and paint must stay decoupled: layout modules are pure functions from content + config → geometry; paint modules are pure recorders from geometry → draw commands.

## Alternatives considered

- **Keep preedit injection for voice status**: Rejected. It couples voice state to the input-method protocol, creates visual inconsistency across client applications, and blocks the preedit channel from being used for real text while voice is active.
- **Give voice its own `wl_surface` or layer-shell surface**: Rejected for Phase 1. A second surface adds protocol complexity, extra GPU resources, and positioning logic. Voice status is transient and small; it belongs near the cursor alongside other input UI. A separate surface may be reconsidered in Phase 4 for large free-floating panels.
- **Use the system tray (SNI) for recording indication**: Rejected as primary UI. The tray is too far from the user’s attention focus during typing. It can remain as a secondary indicator.

## Consequences

- **Positive**: Voice state no longer pollutes the preedit protocol stream. Visual appearance of status indicators is fully controlled by typio (theme, font, colour, HiDPI). Future UI features can hook into the same backend instead of inventing new surfaces.
- **Positive**: The existing flux GPU pipeline, font cache, LRU text layout, and theme system are reused without rewrite.
- **Trade-off**: `zwp_input_popup_surface_v2` visibility depends on the compositor providing a valid `text_input_rectangle`. If a compositor hides the popup when no text input is focused, the voice indicator would also disappear. We accept this because typio holds a keyboard grab during PTT and the input method session is generally active. If real-world testing reveals compositors that break this assumption, a fallback to tray icon or a future `LayerShellProvider` can be added.
- **Trade-off**: The `candidate_popup` code temporarily carries dual responsibility (candidates + status) until Phase 3 zone refactoring is complete.
- **Negative (accepted)**: Phase 1 reuses the preedit text layout path for status text, so status and preedit share the same colour slot in the palette. A dedicated status colour will be added in Phase 2/3.
