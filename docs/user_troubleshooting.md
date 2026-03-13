# Troubleshooting

## `typio --list` shows no external engines

The built-in `basic` engine should still be listed. If it is the only engine, Typio is healthy and simply has no extra plugins installed yet.

Check the external engine directory:

```bash
ls /usr/local/lib/typio/engines
```

## `Failed to connect to Wayland display`

Typio must run inside a Wayland session.

Check:

```bash
echo "$WAYLAND_DISPLAY"
echo "$XDG_SESSION_TYPE"
```

Expected:

- `WAYLAND_DISPLAY` is non-empty
- `XDG_SESSION_TYPE=wayland`

## `Session does not provide the Wayland input-method/text-input protocol stack`

Your compositor is not exposing `zwp_input_method_manager_v2`, or the session is not configured for Typio to use it.

Quick check:

```bash
wayland-info | grep -E 'zwp_input_method_manager_v2|zwp_input_method_v2'
```

If nothing appears, Typio cannot attach as an input method in that session.

Also verify that applications in the session have a usable `text-input-v3`
path:

```bash
wayland-info | grep -E 'zwp_text_input_manager_v3|zwp_text_input_v3'
```

## `Input method unavailable - another input method may be active`

Only one input method can own the protocol seat at a time.

Stop other input method daemons in the same session before starting Typio.

## Typing works poorly or there is no candidate window

That is expected with the built-in `basic` engine. It commits printable characters directly and does not implement candidate UI or advanced composition.

For richer behavior, install an external engine plugin.

With the `rime` plugin, Typio renders candidates through a Wayland popup
surface when the required popup objects are available. If you still only see
inline candidates, Typio likely failed to initialize `wl_compositor`,
`wl_shm`, or the popup surface itself.

If the popup theme does not match your desktop, set one explicitly in `~/.config/typio/engines/rime.conf`:

```ini
popup_theme = dark
candidate_layout = horizontal
```

If the tray shows a generic icon instead of the Rime icon, make sure you are
running a rebuilt binary and that the installed icon theme path is readable.
Typio now prefers the installed `hicolor` theme directory over the source-tree
icon directory when exporting `IconThemePath`, and still provides an
`IconPixmap` fallback for tray hosts that do not resolve themed icons
consistently.

## Debug Logging

```bash
typio --verbose
```

Logs go to stderr. To keep a trace:

```bash
typio --verbose 2>&1 | tee typio.log
```

Keyboard trace lines include both raw key data and the resolved text
character when XKB can derive one, for example `unicode=U+0061 char='a'`.
They also carry `seq=...`, `phase=...`, and `topic=...` so related events can
be correlated in order.

## Validate the Binary and Library

```bash
ldd /usr/local/bin/typio
ldd /usr/local/lib/libtypio-core.so
```

## Useful Report Data

When reporting a problem, include:

- `typio --version`
- `typio --list`
- full `--verbose` output
- compositor name and version
- whether another input method was already running
