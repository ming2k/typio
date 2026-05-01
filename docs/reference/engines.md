# Engine Reference

Built-in and plugin input engines available in Typio.

## Engine Types

| Type | Slot | Purpose |
|------|------|---------|
| `keyboard` | Primary | Key processing, preedit, candidates, commit |
| `voice` | Secondary | Speech-to-text audio inference |

Keyboard and voice selections are independent. Switching one never evicts the other.

---

## Keyboard Engines

### `basic`

Zero-dependency built-in engine. Commits printable Unicode text directly.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `engines.basic.printable_key_mode` | string | `"commit"` | `"commit"` commits directly; `"forward"` forwards through virtual keyboard |
| `engines.basic.compose` | bool | `false` | Enable dead-key compose sequences (e.g. `'` + `a` → `á`) |

Capabilities: `TYPIO_CAP_NONE`

Modes: none (no `get_mode` implementation; always treated as Latin).

Compose sequences (when `compose = true`):

| Sequence | Result |
|----------|--------|
| `'` + vowel | acute accent (á, é, í, ó, ú) |
| `` ` `` + vowel | grave accent (à, è, ì, ò, ù) |
| `^` + vowel | circumflex (â, ê, î, ô, û) |
| `"` + vowel | diaeresis (ä, ë, ï, ö, ü) |
| `~` + `n` | tilde (ñ) |

Escape cancels an active composition and clears preedit.

---

### `rime`

Chinese input powered by [librime](https://github.com/rime/librime). Requires `BUILD_RIME_ENGINE=ON` at compile time.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `engines.rime.shared_data_dir` | string | `/usr/share/rime-data` | System Rime data directory |
| `engines.rime.user_data_dir` | string | `~/.local/share/typio/rime` | Per-user Rime data directory |
| `engines.rime.full_check` | bool | implied by deploy | Controls whether deployment runs a full schema check |

Capabilities:
`TYPIO_CAP_PREEDIT | TYPIO_CAP_CANDIDATES | TYPIO_CAP_PREDICTION | TYPIO_CAP_LEARNING`

Modes:

| `mode_id` | `mode_class` | Label | Icon |
|-----------|--------------|-------|------|
| `chinese` | `NATIVE` | 中 | `typio-rime` |
| `ascii` | `LATIN` | A | `typio-rime-latin` |

Mode is derived from the Rime `ascii_mode` option. Shift toggles it when the schema supports it.

Session behavior:
- One Rime session per `TypioInputContext`, stored as a context property.
- Sessions survive focus churn so runtime options (e.g. `ascii_mode`) are preserved.
- Deployment invalidates all existing sessions; they are recreated lazily on next focus.
- If Rime is still deploying, key presses show a temporary preedit message (`… Rime 正在部署`) instead of blocking.

Config reload rules:
- Changing `shared_data_dir` or `user_data_dir` requires restarting Typio.
- Explicit deploy (via D-Bus or control panel) invalidates generated YAML and triggers a full rebuild.

---

### `mozc`

Japanese input via [Mozc](https://github.com/google/mozc) server IPC. Requires `BUILD_MOZC_ENGINE=ON` at compile time.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `engines.mozc.server_path` | string | `/usr/lib/mozc/mozc_server` | Path to `mozc_server` executable |

Capabilities: `TYPIO_CAP_PREEDIT | TYPIO_CAP_CANDIDATES`

Modes:

| Mozc `CompositionMode` | `mode_class` | ASCII? | Notes |
|------------------------|--------------|--------|-------|
| `DIRECT` | `LATIN` | yes | Direct input |
| `HIRAGANA` | `NATIVE` | no | Default Japanese mode |
| `FULL_KATAKANA` | `NATIVE` | no | Full-width katakana |
| `HALF_KATAKANA` | `NATIVE` | no | Half-width katakana |
| `FULL_ASCII` | `LATIN` | yes | Full-width ASCII |
| `HALF_ASCII` | `LATIN` | yes | Half-width ASCII |

IPC behavior:
- Protocol: `[size:4 LE][protobuf]` over Unix domain socket.
- Socket path resolution order:
  1. Abstract socket matching `@tmp/.mozc.*.session` (read from `/proc/net/unix`).
  2. `$XDG_CONFIG_HOME/mozc/session.ipc`
  3. `~/.mozc/session.ipc`
- If the server is not running, Typio attempts to launch it once per session.
- Each RPC uses a fresh connection with a 300 ms timeout.
- Session creation failure triggers a 3-second retry backoff.

Focus handling:
- `focus_in` re-activates the session if it was left in an ASCII mode.
- `focus_out` submits the current composition (`REVERT`) and clears UI state.
- `reset` sends `RESET_CONTEXT` and clears Typio-side state.

---

## Voice Engines

Voice engines implement `TYPIO_ENGINE_TYPE_VOICE`. They do not handle key events; instead they expose a `TypioVoiceEngineOps` vtable through `engine->voice` that the voice service calls during inference.

### `whisper`

Speech-to-text via [whisper.cpp](https://github.com/ggerganov/whisper.cpp). Requires `BUILD_WHISPER=ON` at compile time.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `engines.whisper.language` | string | `"auto"` | BCP-47 language code or `"auto"` |
| `engines.whisper.model` | string | `"base"` | Model name; loads `~/.local/share/typio/whisper/ggml-<name>.bin` |

Capabilities: `TYPIO_CAP_VOICE_INPUT`

Model file layout:

```text
~/.local/share/typio/
└── whisper/
    └── ggml-<model>.bin
```

Supported model names depend on the whisper.cpp build (commonly `tiny`, `base`, `small`, `medium`, `large`).

Config reload behavior:
- Reload is **non-blocking**: a background thread loads the new model and hot-swaps it.
- The inference thread holds a reference-counted snapshot of the old backend, so in-flight recognition is never interrupted.
- If reload is requested while recording or processing, it is deferred until the job finishes.

---

### `sherpa-onnx`

Speech-to-text via [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx). Requires `BUILD_SHERPA_ONNX=ON` at compile time.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `engines.sherpa-onnx.language` | string | `"auto"` | Language hint (backend-specific interpretation) |
| `engines.sherpa-onnx.model` | string | first found | Subdirectory name under `~/.local/share/typio/sherpa-onnx/` |

Capabilities: `TYPIO_CAP_VOICE_INPUT`

Model directory layout:

```text
~/.local/share/typio/
└── sherpa-onnx/
    └── <model-name>/
        ├── tokens.txt
        ├── model.onnx | model.int8.onnx        # SenseVoice / Paraformer
        ├── encoder.onnx + decoder.onnx          # Whisper / Transducer
        └── joiner.onnx                          # Transducer only
```

Auto-detection:
- If `model` is omitted, Typio scans `sherpa-onnx/` subdirectories and picks the first one with a recognizable file layout.
- Model type is detected from file presence, not from config:

| Detected type | Required files |
|---------------|----------------|
| SenseVoice | `tokens.txt` + `model.int8.onnx` or `model.onnx`; directory name contains `sensevoice` or `sense-voice` |
| Paraformer | `tokens.txt` + `model.int8.onnx` or `model.onnx`; no `sensevoice` in directory name |
| Transducer | `tokens.txt` + `encoder.onnx` + `decoder.onnx` + `joiner.onnx` |
| Whisper (ONNX) | `tokens.txt` + `encoder.onnx` + `decoder.onnx` (no joiner) |

Config reload behavior:
- Same non-blocking hot-swap design as Whisper.

---

## Engine Capability Flags

| Flag | Value | Meaning |
|------|-------|---------|
| `TYPIO_CAP_NONE` | `0` | No special capabilities |
| `TYPIO_CAP_PREEDIT` | `1 << 0` | Engine produces preedit text |
| `TYPIO_CAP_CANDIDATES` | `1 << 1` | Engine produces candidate lists |
| `TYPIO_CAP_PREDICTION` | `1 << 2` | Engine supports prediction |
| `TYPIO_CAP_LEARNING` | `1 << 3` | Engine supports user dictionary learning |
| `TYPIO_CAP_VOICE_INPUT` | `1 << 4` | Engine is a voice/STT backend |

---

## Engine Mode System

Engines that implement `get_mode` expose a structured sub-mode:

```c
typedef struct TypioEngineMode {
    TypioModeClass mode_class;   /* TYPIO_MODE_CLASS_NATIVE or LATIN */
    const char *mode_id;         /* Engine-specific identifier */
    const char *display_label;   /* Short label for tray/popup */
    const char *icon_name;       /* Icon name derived from mode */
} TypioEngineMode;
```

The framework uses `mode_class` for coarse classification (e.g. deciding whether to show a mode indicator) and `display_label` for tray and popup rendering. Engines that implement `get_mode` do not need `get_status_icon`; if both are provided, `get_mode` takes precedence.

Engines notify mode changes by calling `typio_instance_notify_mode()`.

---

## Plugin ABI

External engines are shared objects that export:

```c
const TypioEngineInfo *typio_engine_get_info(void);
TypioEngine *typio_engine_create(void);
```

See [Engine API](api/engine.md) for the full `TypioEngineBaseOps` / `TypioKeyboardEngineOps` / `TypioVoiceEngineOps` vtables and [How to Create a Custom Engine](../how-to/create-custom-engine.md) for a minimal example.

Install path for plugins:

| Build type | Path |
|------------|------|
| System install | `${prefix}/lib/typio/engines/*.so` |
| Custom prefix | `${TYPIO_INSTALL_ENGINEDIR}/*.so` |
