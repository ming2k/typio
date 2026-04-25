# Configuration Reference

Typio uses a single user-facing configuration file in a TOML-compatible format.

## Paths

| Path | Purpose |
|------|---------|
| `$XDG_CONFIG_HOME/typio/typio.toml` | Main config file |
| `$XDG_DATA_HOME/typio` | User data directory |

If `XDG_CONFIG_HOME` or `XDG_DATA_HOME` is unset, Typio falls back to `~/.config/typio` and `~/.local/share/typio`.

## Top-level keys

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `default_engine` | string | first available | Active keyboard engine |
| `default_voice_engine` | string | — | Active voice engine |

## `[display]` section

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `popup_theme` | string | `"auto"` | `"auto"`, `"light"`, or `"dark"` |
| `candidate_layout` | string | `"vertical"` | `"horizontal"` or `"vertical"` |
| `font_size` | int | `11` | Popup text size (6–72) |
| `font_family` | string | `"Sans"` | Font family name |
| `popup_mode_indicator` | bool | `true` | Show engine mode label in popup |

## `[display.colors.light]` and `[display.colors.dark]`

Custom color sections. Colors are 6-digit (`#rrggbb`) or 8-digit (`#rrggbbaa`) hex strings.

| Key | Description |
|-----|-------------|
| `background` | Popup background (RGBA) |
| `border` | Popup border (RGBA) |
| `text` | Candidate text color |
| `muted` | Candidate index labels and mode indicator |
| `preedit` | Preedit text color |
| `selection` | Selected-row highlight (RGBA) |
| `selection_text` | Text color on selected row |

## Engine sections

### `[engines.rime]`

| Key | Type | Description |
|-----|------|-------------|
| `shared_data_dir` | string | Rime shared data directory |
| `user_data_dir` | string | Rime user data directory |

### `[engines.mozc]`

| Key | Type | Description |
|-----|------|-------------|
| `server_path` | string | Path to `mozc_server` |

### `[engines.whisper]`

| Key | Type | Description |
|-----|------|-------------|
| `language` | string | Speech recognition language |
| `model` | string | Model name |

### `[engines.sherpa-onnx]`

| Key | Type | Description |
|-----|------|-------------|
| `language` | string | Speech recognition language |
| `model` | string | Model name |

## Path expansion

For Rime paths, `shared_data_dir` and `user_data_dir` support:

- `~` at the start of the path
- `$VAR`
- `${VAR}`

## Environment variable overrides

| Variable | Effect |
|----------|--------|
| `XDG_CONFIG_HOME` | Overrides config directory |
| `XDG_DATA_HOME` | Overrides data directory |

## See also

- [How to configure](../how-to/configure.md) — task-oriented walkthrough
- [Configuration System](../explanation/configuration-system.md) — design rationale
