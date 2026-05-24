# How to Integrate an Engine

This guide covers adding a new input engine to Typio, either as a built-in integration or as an external plugin.

## Decide: Built-in or Plugin?

| Approach | Use when |
|----------|----------|
| **Built-in** | The engine is maintained by the Typio project, has tight coupling to internal APIs, or is shipped in official packages. |
| **Plugin (shared library)** | The engine is third-party, experimental, or needs to be developed and distributed independently of Typio releases. |

Built-in engines live in `engines/` and are linked into `typio-core` or the daemon. Plugin engines are `.so` files loaded at runtime from the engine directory.

---

## Integrate a Built-in Keyboard Engine

### 1. Create the engine directory

```bash
mkdir engines/myengine
```

### 2. Write the engine implementation

A minimal keyboard engine needs `init`, `destroy`, and `process_key`. See [`engines/basic/basic.c`](../../engines/basic/basic.c) for a concise reference.

Key rules:

- Return `TYPIO_KEY_NOT_HANDLED` for keys you do not consume.
- Do not block inside `process_key`.
- Store engine-specific state in `engine->user_data`.
- Use `typio_input_context_set_composition()` (preedit + candidates in one `TypioComposition`) and `typio_input_context_commit()` to output text ([ADR-0011](../adr/0011-composition-and-lifecycle-rewrite.md); pre-migration code still uses `set_preedit()` / `set_candidates()`).

Example skeleton:

```c
#include <typio/typio.h>

typedef struct {
    /* your state */
} MyEngineData;

static TypioResult my_init(TypioEngine *engine, TypioInstance *instance) {
    MyEngineData *data = calloc(1, sizeof(MyEngineData));
    if (!data) return TYPIO_ERROR_OUT_OF_MEMORY;

    /* load config from instance if needed */
    typio_engine_set_user_data(engine, data);
    return TYPIO_OK;
}

static void my_destroy(TypioEngine *engine) {
    MyEngineData *data = typio_engine_get_user_data(engine);
    free(data);
}

static TypioKeyProcessResult my_process_key(TypioEngine *engine,
                                            TypioInputContext *ctx,
                                            const TypioKeyEvent *event) {
    /* ... handle key ... */
    return TYPIO_KEY_NOT_HANDLED;
}

static const TypioEngineInfo my_info = {
    .name = "myengine",
    .display_name = "My Engine",
    .description = "Example built-in engine",
    .version = "1.0",
    .author = "You",
    .icon = "input-keyboard",
    .language = "und",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
    .capabilities = TYPIO_CAP_PREEDIT | TYPIO_CAP_CANDIDATES,
    .api_version = TYPIO_API_VERSION,
};

static const TypioEngineBaseOps my_base_ops = {
    .init = my_init,
    .destroy = my_destroy,
    .focus_in = my_focus_in,
    .focus_out = my_focus_out,
    .reset = my_reset,
    .reload_config = my_reload_config,
};

static const TypioKeyboardEngineOps my_keyboard_ops = {
    .process_key = my_process_key,
};

/* Built-ins export info/create directly; no macro needed */
const TypioEngineInfo *typio_engine_get_info_myengine(void) {
    return &my_info;
}

TypioEngine *typio_engine_create_myengine(void) {
    return typio_engine_new(&my_info, &my_base_ops, &my_keyboard_ops, NULL);
}
```

### 3. Add build files

`engines/myengine/meson.build`:

```meson
myengine_sources = files('myengine.c')

myengine_lib = static_library('myengine', myengine_sources,
    dependencies: typio_core_dep,
)
```

If your engine needs external libraries, add them via `dependency()` and link against the target.

### 4. Wire into the build system

Add to `meson_options.txt` (or `meson.build`):

```meson
option('build_myengine', type: 'boolean', value: false,
    description: 'Build the MyEngine keyboard engine')
```

In `engines/meson.build`:

```meson
if get_option('build_myengine')
    subdir('myengine')
endif()
```

Add any dependency lookups to the root `meson.build`.

### 5. Register the engine at startup

Edit `daemon/wl_frontend.c` (or whichever frontend initializes the engine manager):

```c
#ifdef BUILD_MYENGINE
extern const TypioEngineInfo *typio_engine_get_info_myengine(void);
extern TypioEngine *typio_engine_create_myengine(void);
#endif

/* ... during initialization ... */
#ifdef BUILD_MYENGINE
    typio_engine_manager_register(manager,
                                  typio_engine_create_myengine,
                                  typio_engine_get_info_myengine);
#endif
```

### 6. Add config schema

If your engine reads configuration from `typio.toml`, add the schema in the config system so keys are validated. The exact location depends on the current config schema implementation; look for existing engine schemas (e.g. `engines.rime`) and mirror them.

---

## Integrate a Built-in Voice Engine

Voice engines use the same `TypioEngineBaseOps` as keyboard engines for lifecycle management, but instead of a `TypioKeyboardEngineOps` vtable they provide a `TypioVoiceEngineOps` vtable with `process_audio`.

### 1. Implement the engine

```c
static const TypioEngineInfo my_voice_info = {
    .name = "my-voice",
    .display_name = "My Voice",
    .type = TYPIO_ENGINE_TYPE_VOICE,
    .capabilities = TYPIO_CAP_VOICE_INPUT,
    .api_version = TYPIO_API_VERSION,
};

static TypioResult my_voice_init(TypioEngine *engine, TypioInstance *instance) {
    /* load model, store in engine->user_data */
    return TYPIO_OK;
}

static void my_voice_destroy(TypioEngine *engine) {
    /* free model resources */
}

static char *my_voice_process_audio(TypioEngine *engine,
                                     const float *samples, size_t n_samples) {
    /* Run inference, return heap-allocated text or NULL */
}

static const TypioEngineBaseOps my_voice_base_ops = {
    .init = my_voice_init,
    .destroy = my_voice_destroy,
    .focus_in = my_voice_focus_in,
    .focus_out = my_voice_focus_out,
    .reset = my_voice_reset,
    .reload_config = my_voice_reload_config,
};

static const TypioVoiceEngineOps my_voice_engine_ops = {
    .process_audio = my_voice_process_audio,
};
```

Audio format contract:
- Samples are PCM float32.
- Mono, 16 kHz.
- `n_samples` is the frame count (not byte count).

### 2. Non-blocking reload (optional)

Follow the proxy pattern used in `voice_engine_whisper.c` and `voice_engine_sherpa.c` if you want config reloads to happen in a background thread:

### 3. Export backend discovery

Add to `daemon/voice/voice_engine.h`:

```c
#ifdef HAVE_MY_VOICE
extern const TypioEngineInfo *typio_engine_get_info_my_voice(void);
extern TypioEngine *typio_engine_create_my_voice(void);
#endif
```

Add to `daemon/voice/voice_service.c` or the voice engine compilation unit:

```c
#ifdef HAVE_MY_VOICE
    typio_engine_manager_register(manager,
                                  typio_engine_create_my_voice,
                                  typio_engine_get_info_my_voice);
#endif
```

### 4. Update build files

- Add `option('build_my_voice', type: 'boolean', value: false)` in `meson_options.txt` (or `meson.build`).
- If the backend needs PipeWire, guard it with `if build_my_voice or build_whisper`.
- Set `HAVE_MY_VOICE` in `typio_build_config.h.in` and wire it in `meson.build`.

---

## Integrate a Plugin Engine

For external, independently developed engines, use the shared-library plugin ABI.

### Required exported symbols

```c
const TypioEngineInfo *typio_engine_get_info(void);
TypioEngine *typio_engine_create(void);
```

Use the `TYPIO_ENGINE_DEFINE(info, create)` macro to generate both symbols.

### Build and install

```meson
project('typio-my-plugin', 'c',
    meson_version: '>=1.0.0')

typio_dep = dependency('typio')

my_plugin = shared_module('typio-my-plugin', 'my_plugin.c',
    dependencies: typio_dep,
    install: true,
    install_dir: get_option('libdir') / 'typio' / 'engines',
)
```

### Verify

```bash
typio-daemon --list
typio-daemon --engine my-plugin --verbose
```

See [How to Create a Custom Engine](create-custom-engine.md) for a complete minimal example.

---

## Testing a New Engine

1. **Unit test** — If the engine has pure logic (e.g. a key parser), add tests under `tests/`.
2. **Integration test** — Run `typio-daemon --engine <name>` and exercise key sequences with `typio-daemon --verbose`.
3. **Config reload test** — Change the engine's `typio.toml` section and trigger reload (SIGHUP or D-Bus) to verify `reload_config` behavior.

---

## Checklist

- [ ] Engine implements required ops (`init`, `destroy`, `process_key` for keyboard; `init`, `destroy`, `process_audio` for voice).
- [ ] `api_version` matches `TYPIO_API_VERSION`.
- [ ] `type` is `TYPIO_ENGINE_TYPE_KEYBOARD` or `TYPIO_ENGINE_TYPE_VOICE`.
- [ ] For voice: backend conforms to float32 mono 16 kHz contract.
- [ ] `process_key` never blocks.
- [ ] Engine state is stored in `user_data`, not globals.
- [ ] Meson option added and wired into `engines/meson.build`.
- [ ] Engine registered in the daemon startup path (built-in only).
- [ ] Config schema updated if new keys are introduced.
- [ ] Documentation updated: `docs/reference/engines.md` and this guide.
