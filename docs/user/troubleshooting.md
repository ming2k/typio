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

If edits under `~/.local/share/typio/rime` do not affect candidate behavior,
switch to the `rime` engine and run:

```bash
typio-client rime deploy
```

That asks the daemon to rerun librime deployment and regenerate
`~/.local/share/typio/rime/build/*.yaml` from your source YAML files.

If the popup looks blurry on a 2x HiDPI output, make sure you are running a
recent Typio build. The popup now follows `wl_surface.enter/leave` and
`wl_output.scale`, so integer output scales should render sharply after a
restart of the daemon.

If the popup theme does not match your desktop, set one explicitly in
`~/.config/typio/typio.toml`:

```toml
[engines.rime]
popup_theme = "dark"
candidate_layout = "horizontal"
```

If the tray shows a generic icon instead of the Rime icon, make sure you are
running a rebuilt binary and that the installed icon theme path is readable.
Typio now prefers the installed `hicolor` theme directory over the source-tree
icon directory when exporting `IconThemePath`, and still provides an
`IconPixmap` fallback for tray hosts that do not resolve themed icons
consistently.

## Candidate Navigation Becomes Sluggish After Extended Runtime

**Symptom:** Normal typing remains smooth, but scrolling through the candidate
list with arrow keys or page keys becomes visibly delayed or appears to skip
frames. The problem only appears after the daemon has been running for several
hours and worsens over time.

**Root cause (fixed in current builds):** Each time a Wayland keymap event was
received from the compositor, the daemon duplicated the file descriptor and sent
the copy to the virtual keyboard, but never closed the duplicate. Every
application focus switch leaks one file descriptor. The Linux per-process fd
limit is 1024 by default; as the table fills up, calls that need a new fd (such
as `mkstemp` inside the SHM buffer allocator) start failing. Failed buffer
allocation causes candidate popup frames to be silently dropped during
navigation.

**Diagnosis:** While the daemon is running, count the leaked entries:

```bash
ls -la /proc/$(pidof typio)/fd | grep '/run/user/.*deleted' | wc -l
```

A healthy process shows zero or a very small number. Hundreds of entries confirm
the leak.

**Fix:** Restart the daemon to clear the accumulated file descriptors immediately:

```bash
typio-client stop
typio &
```

The underlying leak is patched in the current source. Rebuilding and
reinstalling eliminates it permanently.

## Debug Logging

```bash
typio --verbose
```

Logs go to stderr. To keep a trace:

```bash
typio --verbose 2>&1 | tee typio.log
```

Or use the helper script:

```bash
bash scripts/capture_typio_trace.sh /tmp/typio-trace.log
```

Keyboard trace lines include both raw key data and the resolved text
character when XKB can derive one, for example `unicode=U+0061 char='a'`.
They also carry `seq=...`, `phase=...`, and `topic=...` so related events can
be correlated in order.

## Wayland Runtime Diagnostics

Current builds expose two complementary diagnostics surfaces:

- continuous logs through stderr (visible in the terminal when running
  manually with `--verbose`, or in the compositor's log stream when started
  via desktop autostart)
- a structured D-Bus `RuntimeState` property on `org.typio.InputMethod1`

Use both. Logs show the event history; `RuntimeState` shows the current
frontend health snapshot.

Read the last fault snapshot from the recent-log dump:

```bash
find ~/.local/state/typio/logs -type f -name '*.log' 2>/dev/null
rg -n "Virtual keyboard|fail-safe|keymap timeout|Keyboard grab|Wayland text UI slow|Popup slow render|Rime sync slow" ~/.local/state/typio/logs/latest.log

# Popup slow-render lines now include delta= (selection/content/aux/style)
# and omit the old fast_path= field.
```

Current builds keep a single persisted recent-log snapshot at
`~/.local/state/typio/logs/latest.log`, replacing that file each time a new
dump is written.

Read the live Wayland runtime state:

```bash
gdbus call --session \
  --dest org.typio.InputMethod1 \
  --object-path /org/typio/InputMethod1 \
  --method org.freedesktop.DBus.Properties.Get \
  org.typio.InputMethod1 RuntimeState
```

Watch runtime-state transitions live:

```bash
gdbus monitor --session \
  --dest org.typio.InputMethod1 \
  --object-path /org/typio/InputMethod1
```

The most useful `RuntimeState` fields for troubleshooting are:

- `lifecycle_phase`: should normally settle at `active` while typing
- `keyboard_grab_active`: whether Typio still owns the Wayland keyboard grab
- `virtual_keyboard_state`: `ready`, `needs_keymap`, `broken`, or `absent`
- `virtual_keyboard_has_keymap`: whether a compositor keymap has reached the
  virtual keyboard path
- `active_key_generation`: the current keyboard-grab generation
- `virtual_keyboard_keymap_generation`: which generation most recently reached
  the virtual keyboard keymap path
- `virtual_keyboard_drop_count`: how many forwarded events were dropped because
  vk was not ready
- `virtual_keyboard_state_age_ms`: how long the current vk state has been held
- `virtual_keyboard_keymap_age_ms`: how long since the last successful keymap
- `virtual_keyboard_forward_age_ms`: how long since the last successful vk
  forward
- `virtual_keyboard_keymap_deadline_remaining_ms`: remaining time before
  Typio treats a missing keymap as a fail-safe condition
- `watchdog_armed`: whether the frontend watchdog is currently protecting an
  active keyboard-grab path

Interpret the most common bad combinations like this:

- `keyboard_grab_active=true` and `virtual_keyboard_state=needs_keymap` for too
  long: grab rebuild happened, but the compositor-to-vk keymap chain did not
  finish cleanly
- `virtual_keyboard_state=broken`: vk has been declared unusable and Typio
  should fail open instead of continuing to swallow keys
- increasing `virtual_keyboard_drop_count`: Typio is receiving input it cannot
  safely forward to the focused client

For the complete diagnostic meaning of these fields and the underlying timing
model, see [Developer Timing Model](../dev/timing-model.md).

If you report a timing or freeze problem, include both:

- the relevant `--verbose` log window
- a `RuntimeState` snapshot taken while the fault is happening

## When Input and Tray Both Stop Responding

If Typio still starts but later all Typio-routed input stops working and the
tray menu also stops responding:

```bash
bash scripts/capture_typio_trace.sh /tmp/typio-freeze.log
```

Reproduce the failure, then keep:

- `/tmp/typio-freeze.log`
- whether `Ctrl+Shift+Escape` still exits Typio
- compositor name and version
- the last `poll error`, `dispatch failure`, `grab`, `key`, and `im` lines

## Emergency Recovery

The keyboard shortcut emergency exit is an in-band shortcut. It depends on the
same keyboard event path that may already be degraded during a severe Wayland
failure. Treat it as a convenience, not as the only recovery path.

Preferred out-of-band recovery paths:

```bash
gdbus call --session \
  --dest org.typio.InputMethod1 \
  --object-path /org/typio/InputMethod1 \
  --method org.typio.InputMethod1.Stop
```

```bash
pkill -TERM typio
```

If Typio is holding the keyboard grab but cannot safely forward keys, current
builds are designed to fail open: they release the grab and stop instead of
continuing to swallow input.

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
