# Voice Input Architecture

Typio's voice input is a secondary pipeline that runs alongside the active keyboard engine. It does not replace keyboard input; the two are selected independently and operate in parallel.

## Design Goals

1. **Non-blocking inference** — Audio capture and speech recognition must not stall the Wayland event loop.
2. **Hot model swap** — Changing voice engine or model config at runtime must not interrupt an in-flight recognition job.
3. **Backend agnosticism** — The voice service owns audio and threading; backends only load models and run inference.
4. **Graceful degradation** — If the model is missing or the backend fails to load, voice input is simply unavailable; the daemon keeps running.

## Component Layers

```text
┌─────────────────────────────────────────────────────────────┐
│  typio daemon (main thread, Wayland event loop)             │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────┐  │
│  │ key_route   │    │ engine_mgr  │    │ voice_service   │  │
│  │             │───→│             │───→│ (state machine) │  │
│  └─────────────┘    └─────────────┘    └─────────────────┘  │
│                                               │             │
│                    eventfd notification ←─────┘             │
│                    (inference thread completion)            │
└─────────────────────────────────────────────────────────────┘
         │                              │
         ▼                              ▼
┌─────────────────┐            ┌─────────────────┐
│ PipeWire capture│            │ voice_engine_*  │
│ (audio callback)│            │ (engine adapter)│
│                 │            │ TypioVoiceEngine│
│                 │            │ Ops.process_audio│
└─────────────────┘            └─────────────────┘
                                        │
                                        ▼
                              ┌─────────────────┐
                              │  Backend proxy  │
                              │ (refcount swap) │
                              └─────────────────┘
                                        │
                    ┌───────────────────┼───────────────────┐
                    ▼                   ▼                   ▼
           ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
           │ whisper.cpp │    │ sherpa-onnx │    │   (future)  │
           └─────────────┘    └─────────────┘    └─────────────┘
```

## State Machine

The voice service has three states:

| State | Transitions | Description |
|-------|-------------|-------------|
| `IDLE` | `start()` → `RECORDING` | No audio capture; backend may be present or absent |
| `RECORDING` | `stop()` → `PROCESSING` | PipeWire callback appends float32 samples to a growable buffer |
| `PROCESSING` | inference done → `IDLE` | A detached pthread runs `engine->voice->process_audio()`; result arrives via `eventfd` |

The main thread never blocks on inference. When `eventfd` becomes readable, `voice_service_dispatch()` joins the thread, retrieves the text result, and commits it through the focused `TypioInputContext`.

## Backend Proxy Pattern

Both Whisper and Sherpa-ONNX adapters use the same proxy design. The proxy is internal to the engine; the voice service sees only the `TypioVoiceEngineOps.process_audio` callback.

```
Voice service (inference thread)
    │
    ▼
┌─────────────────┐
│  engine->voice  │
│  process_audio()│
└─────────────────┘
    │
    ▼
┌─────────────┐    refcount    ┌─────────────┐
│   Proxy     │◄───────────────│   Impl      │
│ (mutex)     │                │ (backend)   │
└─────────────┘                └─────────────┘
```

- The **proxy** lives inside the engine's `user_data` and reference-counts the real backend.
- `process_audio()` increments the refcount under lock, delegates to the real backend, then decrements.
- A **reload thread** loads a new model, swaps `proxy->impl` under the same lock, and parks the old impl in `pending_destroy` if the refcount is non-zero.
- This guarantees that an in-flight inference call always sees a valid backend even while a hot-swap is happening.

## Audio Pipeline

- **Format**: PCM float32, mono, 16 kHz.
- **Capture**: PipeWire via `typio_pw_capture_*`.
- **Buffer**: Pre-allocated for 30 seconds; grows dynamically if the user holds PTT longer.
- **Threading**: The audio callback runs on a PipeWire realtime thread. It only copies samples into the buffer under `buffer_mutex` and never allocates memory.

## Reload Semantics

Voice config reloads follow these rules:

1. If the service is `IDLE`, the new engine/model is activated immediately.
2. If the service is `RECORDING` or `PROCESSING`, the reload is **deferred** (`reload_pending = true`).
3. When the current job finishes and the state returns to `IDLE`, the deferred reload is applied before the next `start()`.

This avoids tearing down a backend while it is still needed.

## Integration with Engine Manager

Voice engines are registered in `TypioEngineManager` with type `TYPIO_ENGINE_TYPE_VOICE`. The engine manager tracks two independent active slots:

- `active_keyboard_index` — for keyboard engines
- `active_voice_index` — for voice engines

`typio_engine_manager_set_active("whisper")` routes to the voice slot. `typio_engine_manager_set_active_voice("whisper")` is the explicit voice-only variant.

The voice service snapshots the active voice engine at startup and after each reload. It does not own the engine; the engine manager owns lifecycle and destruction.

## Failure Modes

| Symptom | Cause | User-visible effect |
|---------|-------|---------------------|
| Voice unavailable, reason "no voice engine active" | No voice engine selected or built | PTT shortcut does nothing |
| Voice unavailable, reason "voice backend failed to initialize" | Model file missing or incompatible | PTT shortcut does nothing |
| Voice unavailable, reason "audio capture unavailable" | PipeWire not running or permission denied | PTT shortcut does nothing |
| Empty recognition result | Silent audio or model mismatch | Nothing committed |

All failure modes are logged at `TYPIO_LOG_WARNING` or `TYPIO_LOG_ERROR`.
