# How to Integrate an Engine

This guide covers adding a new input engine to Typio, either as a built-in integration or as an external plugin.

## Decide: Built-in or Plugin?

| Approach | Use when |
|----------|----------|
| **Built-in** | The engine is maintained by the Typio project, has tight coupling to internal APIs, or is shipped in official packages. |
| **Plugin (shared library)** | The engine is third-party, experimental, or needs to be developed and distributed independently of Typio releases. |

Built-in engines live in `src/engines/` and are linked into `typio-core` or the daemon. Plugin engines are `.so` files loaded at runtime from the engine directory.

---

## Integrate a Built-in Keyboard Engine

### 1. Create the engine directory

```bash
mkdir src/engines/myengine
```

### 2. Write the engine implementation

A minimal keyboard engine needs `init`, `destroy`, and `process_key`. See [`src/engines/basic/basic.c`](../../src/engines/basic/basic.c) for a concise reference.

Key rules:

- Return `TYPIO_KEY_NOT_HANDLED` for keys you do not consume.
- Do not block inside `process_key`.
- Store engine-specific state in `engine->user_data`.
- Use `typio_input_context_commit()`, `set_preedit()`, and `set_candidates()` to output text.

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

### 3. Add `CMakeLists.txt`

```cmake
add_library(myengine STATIC myengine.c)
target_link_libraries(myengine PRIVATE typio-core)
```

If your engine needs external libraries (e.g. `pkg_check_modules`), add them here and link against the target.

### 4. Wire into the build system

Edit `src/engines/CMakeLists.txt`:

```cmake
option(BUILD_MYENGINE "Build the MyEngine keyboard engine" OFF)
if(BUILD_MYENGINE)
    add_subdirectory(myengine)
endif()
```

Edit the root `CMakeLists.txt` to add the option and any `find_package` / `pkg_check_modules` calls.

### 5. Register the engine at startup

Edit `src/apps/typio/wl_frontend.c` (or whichever frontend initializes the engine manager):

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

Add to `src/apps/typio/voice/voice_engine.h`:

```c
#ifdef HAVE_MY_VOICE
extern const TypioEngineInfo *typio_engine_get_info_my_voice(void);
extern TypioEngine *typio_engine_create_my_voice(void);
#endif
```

Add to `src/apps/typio/voice/voice_service.c` or the voice engine compilation unit:

```c
#ifdef HAVE_MY_VOICE
    typio_engine_manager_register(manager,
                                  typio_engine_create_my_voice,
                                  typio_engine_get_info_my_voice);
#endif
```

### 4. Update `CMakeLists.txt`

- Add `option(BUILD_MY_VOICE ...)` in root `CMakeLists.txt`.
- If the backend needs PipeWire, guard it with `if(BUILD_MY_VOICE OR BUILD_WHISPER OR ...)`.
- Set `HAVE_MY_VOICE` in `typio_build_config.h.in`.

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

```cmake
cmake_minimum_required(VERSION 3.16)
project(typio-my-plugin C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TYPIO REQUIRED typio)

add_library(typio-my-plugin MODULE my_plugin.c)
target_include_directories(typio-my-plugin PRIVATE ${TYPIO_INCLUDE_DIRS})
target_link_libraries(typio-my-plugin PRIVATE typio-core)

install(TARGETS typio-my-plugin
    LIBRARY DESTINATION lib/typio/engines)
```

### Verify

```bash
typio --list
typio --engine my-plugin --verbose
```

See [How to Create a Custom Engine](create-custom-engine.md) for a complete minimal example.

---

## Testing a New Engine

1. **Unit test** — If the engine has pure logic (e.g. a key parser), add tests under `tests/`.
2. **Integration test** — Run `typio --engine <name>` and exercise key sequences with `typio --verbose`.
3. **Config reload test** — Change the engine's `typio.toml` section and trigger reload (SIGHUP or D-Bus) to verify `reload_config` behavior.

---

## Checklist

- [ ] Engine implements required ops (`init`, `destroy`, `process_key` for keyboard; `init`, `destroy`, `process_audio` for voice).
- [ ] `api_version` matches `TYPIO_API_VERSION`.
- [ ] `type` is `TYPIO_ENGINE_TYPE_KEYBOARD` or `TYPIO_ENGINE_TYPE_VOICE`.
- [ ] For voice: backend conforms to float32 mono 16 kHz contract.
- [ ] `process_key` never blocks.
- [ ] Engine state is stored in `user_data`, not globals.
- [ ] CMake option added and wired into `src/engines/CMakeLists.txt`.
- [ ] Engine registered in the daemon startup path (built-in only).
- [ ] Config schema updated if new keys are introduced.
- [ ] Documentation updated: `docs/reference/engines.md` and this guide.
