# How to Configure Typio

This guide assumes you have already [built and run Typio](../tutorials/01-getting-started.md).

## When to use this

Use this when you want to change Typio behavior, appearance, or engine settings.

## Configuration file

Typio uses a single user-facing file in TOML-compatible format:

```text
~/.config/typio/typio.toml
```

If `XDG_CONFIG_HOME` is unset, Typio falls back to `~/.config/typio`.

## Edit the config file

Create or edit the file:

```bash
mkdir -p ~/.config/typio
$EDITOR ~/.config/typio/typio.toml
```

Minimal example:

```toml
default_engine = "basic"
```

Typio reloads the config automatically when the file changes. You can also trigger a reload manually:

```bash
typio config reload
```

## Verify the active config

```bash
typio config get
```

## CLI overrides

For a single run, override config paths or engine choice without editing files:

```bash
typio --config ~/.config/typio-dev --data ~/.local/share/typio-dev --engine-dir ~/.local/lib/typio/engines --engine basic
```

Available overrides:

- `--config DIR` — config directory
- `--data DIR` — data directory
- `--engine-dir DIR` — extra engine search path
- `--engine NAME` — active engine for this run

## Common configuration tasks

### Change the popup theme

```toml
[display]
popup_theme = "dark"
```

### Change candidate layout

```toml
[display]
candidate_layout = "vertical"
```

### Configure the Rime engine

```toml
[engines.rime]
shared_data_dir = "/usr/share/rime-data"
user_data_dir = "~/.local/share/typio/rime"
```

After editing Rime source files directly, deploy:

```bash
typio rime deploy
```

## See also

- [Configuration Reference](../reference/configuration.md) — exhaustive key listing
- [Configuration System](../explanation/configuration-system.md) — design rationale
