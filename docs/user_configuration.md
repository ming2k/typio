# Configuration

Typio follows XDG-style defaults for user data and configuration.

## Default Paths

- Main config: `$XDG_CONFIG_HOME/typio/typio.conf`
- Engine config files: `$XDG_CONFIG_HOME/typio/engines/<engine>.conf`
- User data: `$XDG_DATA_HOME/typio`
- External engine directory at runtime: install-time default such as `/usr/local/lib/typio/engines`

If `XDG_CONFIG_HOME` or `XDG_DATA_HOME` is unset, Typio falls back to:

- `~/.config/typio`
- `~/.local/share/typio`

## Core Configuration Keys

At the moment, the core daemon consumes one stable top-level key:

```ini
default_engine = basic
```

If `default_engine` is omitted, Typio activates the first available engine. With a fresh install that is usually the built-in `basic` engine.

If the Rime plugin is installed and you want it to start by default:

```ini
default_engine = rime
```

## Engine-Specific Configuration

External engines receive a per-engine config path automatically:

```text
$XDG_CONFIG_HOME/typio/engines/<engine>.conf
```

Typio itself does not interpret those files. Each engine decides what keys it supports.

For `rime`, the shipped example is:

```text
share/typio/engines/rime.conf.example
```

Supported `rime` keys:

```ini
schema = luna_pinyin
page_size = 9
popup_theme = auto
candidate_layout = horizontal
# shared_data_dir = /usr/share/rime-data
# user_data_dir = ~/.local/share/typio/rime
# full_check = false
```

Meaning of the popup-related keys:

- `popup_theme = auto|light|dark`
- `candidate_layout = horizontal|vertical`

`popup_theme = auto` is best-effort. Typio does not get a theme directly from the Wayland input-method protocol, so it infers light/dark preference from common desktop settings such as GTK dark-theme preference, `GTK_THEME`, and KDE color-scheme hints.

For `rime`, `shared_data_dir` and `user_data_dir` support:

- `~` at the start of the path
- `$VAR`
- `${VAR}`

## CLI Overrides

These command-line flags override the default search paths or engine choice for a single run:

- `--config DIR`
- `--data DIR`
- `--engine-dir DIR`
- `--engine NAME`

Example:

```bash
typio --config ~/.config/typio-dev --data ~/.local/share/typio-dev --engine-dir ~/.local/lib/typio/engines --engine basic
```

## Example `typio.conf`

```ini
# Use the built-in basic keyboard engine
default_engine = basic

# Or switch to Rime when the plugin is installed
# default_engine = rime
```

The repository also ships [`data/typio.conf.example`](../data/typio.conf.example) as a starting point.
