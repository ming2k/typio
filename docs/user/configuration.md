# Configuration

Typio uses a single user-facing configuration file in a TOML-compatible format.

## Default Paths

- Main config: `$XDG_CONFIG_HOME/typio/typio.toml`
- User data: `$XDG_DATA_HOME/typio`
- External engine directory at runtime: install-time default such as `/usr/local/lib/typio/engines`

If `XDG_CONFIG_HOME` or `XDG_DATA_HOME` is unset, Typio falls back to:

- `~/.config/typio`
- `~/.local/share/typio`

## Configuration Model

`typio.toml` is the only supported configuration source. Built-in integrations
such as Rime, Mozc, and Whisper read from sections inside that file.

Minimal example:

```toml
default_engine = "basic"
```

If `default_engine` is omitted, Typio activates the first available engine.

Typical full example:

```toml
default_engine = "rime"
default_voice_engine = "whisper"

[engines.rime]
# shared_data_dir = "/usr/share/rime-data"
# user_data_dir = "~/.local/share/typio/rime"

[engines.mozc]
# server_path = "/usr/lib/mozc/mozc_server"

[engines.whisper]
language = "zh"
model = "base"

# [engines.sherpa-onnx]
# language = "auto"
# model = "sensevoice-small"
```

The repository ships [`data/typio.toml.example`](../data/typio.toml.example)
as a starting point.

## Supported Built-In Keys

Top level:

- `default_engine`
- `default_voice_engine`

Display section: `[display]`

- `popup_theme` — `"auto"`, `"light"`, or `"dark"` (default: `"auto"`)
  - `"auto"` infers the preference from desktop settings (GTK dark theme, KDE color scheme, etc.)
- `candidate_layout` — `"horizontal"` or `"vertical"` (default: `"vertical"`)
- `font_size` — popup text size, 6–72 (default: `11`). Candidate index labels are
  automatically rendered at ~80% of this size so they read as secondary hints rather
  than competing with the candidate text.
- `font_family` — font family name (default: `"Sans"`); e.g. `"Noto Sans CJK SC"`
- `popup_mode_indicator` — show engine mode label in popup (default: `true`)

Custom color schemas: `[display.colors.light]` and `[display.colors.dark]`

Each section customises one variant of the popup. Custom values override the
corresponding built-in palette; the active variant is whichever one `popup_theme`
resolves to for the current session (light or dark). Omit any key to keep the
built-in default for that channel. Colors are 6-digit (`#rrggbb`) or 8-digit
(`#rrggbbaa`) hex strings.

Available keys (same for both sections):

- `background` — popup background (RGBA)
- `border` — popup border (RGBA)
- `text` — candidate text color
- `muted` — candidate index labels and mode indicator
- `preedit` — preedit text color
- `selection` — selected-row highlight (RGBA)
- `selection_text` — text color on selected row

Example — softer accent colors for both light and dark:

```toml
[display]
popup_theme = "auto"

[display.colors.light]
selection = "#2f9e44f5"
muted     = "#868e96"

[display.colors.dark]
selection = "#2f9e44f5"
muted     = "#666666"
```

Rime section: `[engines.rime]`

- `shared_data_dir`
- `user_data_dir`

Mozc section: `[engines.mozc]`

- `server_path`

Whisper section: `[engines.whisper]`

- `language`
- `model`

Sherpa-ONNX section: `[engines.sherpa-onnx]`

- `language`
- `model`

`default_voice_engine` selects the active speech-recognition backend. Typio
keeps voice engines separate from keyboard engines, so changing
`default_voice_engine` does not replace the active keyboard engine.

Voice backend and model reloads happen in the background after config reload,
so the daemon stays responsive while a replacement model is loaded.

`popup_theme = "auto"` is best-effort. Typio does not get a theme directly
from the Wayland input-method protocol, so it infers light/dark preference
from common desktop settings such as GTK dark-theme preference, `GTK_THEME`,
and KDE color-scheme hints.

For Rime, `shared_data_dir` and `user_data_dir` support:

- `~` at the start of the path
- `$VAR`
- `${VAR}`

The active Rime schema is remembered in XDG state and changed through Typio's
runtime controls. It is no longer configured in `typio.toml`.

If you edit Rime source files under `user_data_dir` directly, such as
`default.custom.yaml`, run `typio-client rime deploy` or use the control-panel
deploy action to rebuild generated `build/*.yaml` artifacts.

## CLI Overrides

These command-line flags override the default search paths or engine choice for
a single run:

- `--config DIR`
- `--data DIR`
- `--engine-dir DIR`
- `--engine NAME`

Example:

```bash
typio --config ~/.config/typio-dev --data ~/.local/share/typio-dev --engine-dir ~/.local/lib/typio/engines --engine basic
```
