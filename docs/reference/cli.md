# CLI Reference

## Daemon flags

| Flag | Description |
|------|-------------|
| `--config DIR` | Config directory override |
| `--data DIR` | Data directory override |
| `--engine-dir DIR` | Extra engine search path |
| `--engine NAME` | Active engine for this run |
| `--list` | List available engines and exit |
| `--verbose` | Enable debug logging to stderr |
| `--version` | Print version and exit |
| `--help` | Print help and exit |

## Client subcommands

The `typio` executable also acts as a lightweight CLI that controls a running daemon over D-Bus.

```bash
typio engine             # print active keyboard engine
typio engine list        # list engines (* marks active)
typio engine next        # cycle to next engine
typio engine rime        # switch to rime
typio rime schema        # print current Rime schema
typio rime deploy        # rebuild generated Rime config files
typio rime schema luna_pinyin # set Rime schema
typio config reload      # reload config from disk
typio config get         # print current config text
typio config set "..."   # replace config text
typio status             # show server status summary
typio stop               # stop the daemon
typio version            # show server version
typio help               # show help
```

Use `typio rime deploy` after editing Rime source files under Typio's user data directory, such as `default.custom.yaml`, so librime rebuilds the generated `build/*.yaml` files before the next composition session.
