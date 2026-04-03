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

[engines.rime]
# shared_data_dir = "/usr/share/rime-data"
# user_data_dir = "~/.local/share/typio/rime"

[engines.mozc]
# server_path = "/usr/lib/mozc/mozc_server"

[whisper]
language = "zh"
model = "base"
```

The repository ships [`data/typio.toml.example`](../data/typio.toml.example)
as a starting point.

## Supported Built-In Keys

Top level:

- `default_engine`

Display section: `[display]`

- `popup_theme` — `"auto"`, `"light"`, or `"dark"` (default: `"auto"`)
- `candidate_layout` — `"horizontal"` or `"vertical"` (default: `"horizontal"`)
- `font_size` — popup text size, 6–72 (default: `11`)
- `popup_mode_indicator` — show engine mode label in popup (default: `true`)

Rime section: `[engines.rime]`

- `shared_data_dir`
- `user_data_dir`

Mozc section: `[engines.mozc]`

- `server_path`

Whisper section: `[whisper]`

- `language`
- `model`

`popup_theme = "auto"` is best-effort. Typio does not get a theme directly
from the Wayland input-method protocol, so it infers light/dark preference
from common desktop settings such as GTK dark-theme preference, `GTK_THEME`,
and KDE color-scheme hints.

For Rime, `shared_data_dir` and `user_data_dir` support:

- `~` at the start of the path

The active Rime schema is remembered in XDG state and changed through Typio's
runtime controls. It is no longer configured in `typio.toml`.
- `$VAR`
- `${VAR}`

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
