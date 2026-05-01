# How to Create a Custom Engine

This guide assumes you are familiar with building Typio from source and with C programming.

## When to use this

Use this when you want to add a new input engine to Typio as a shared-library plugin.

## Prerequisites

- Typio built and installed (or at least `typio-core` headers and pkg-config file available)
- C compiler and CMake

## Required exported symbols

Your shared object must export:

```c
const TypioEngineInfo *typio_engine_get_info(void);
TypioEngine *typio_engine_create(void);
```

## Minimal keyboard engine

```c
#include <typio/typio.h>

/* ── Base operations (mandatory for every engine) ─────────────────────── */

static TypioResult my_init(TypioEngine *engine, TypioInstance *instance) {
    (void)engine;
    (void)instance;
    return TYPIO_OK;
}

static void my_destroy(TypioEngine *engine) {
    (void)engine;
}

static void my_focus_in(TypioEngine *engine, TypioInputContext *ctx) {
    (void)engine;
    (void)ctx;
    /* No per-context state to restore. */
}

static void my_focus_out(TypioEngine *engine, TypioInputContext *ctx) {
    (void)engine;
    /* Cancel any pending composition on focus loss. */
    typio_input_context_clear_preedit(ctx);
}

static void my_reset(TypioEngine *engine, TypioInputContext *ctx) {
    (void)engine;
    /* Cancel any pending composition on explicit reset. */
    typio_input_context_clear_preedit(ctx);
}

static TypioResult my_reload_config(TypioEngine *engine) {
    (void)engine;
    return TYPIO_OK;
}

/* ── Keyboard operations (mandatory for keyboard engines) ─────────────── */

static TypioKeyProcessResult my_process_key(TypioEngine *engine,
                                            TypioInputContext *ctx,
                                            const TypioKeyEvent *event) {
    (void)engine;

    if (!ctx || !event || event->type != TYPIO_EVENT_KEY_PRESS) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    if (event->keysym == TYPIO_KEY_space) {
        typio_input_context_commit(ctx, "hello");
        return TYPIO_KEY_COMMITTED;
    }

    return TYPIO_KEY_NOT_HANDLED;
}

/* ── Metadata ────────────────────────────────────────────────────────── */

static const TypioEngineInfo my_info = {
    .name = "my-engine",
    .display_name = "My Engine",
    .description = "Example Typio engine",
    .version = "0.1.0",
    .author = "You",
    .icon = "input-keyboard",
    .language = "und",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
    .capabilities = TYPIO_CAP_NONE,
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

static TypioEngine *my_create(void) {
    return typio_engine_new(&my_info, &my_base_ops, &my_keyboard_ops, NULL);
}

TYPIO_ENGINE_DEFINE(my_info, my_create)
```

## Minimal voice engine

Voice engines provide base operations **and** a `TypioVoiceEngineOps` vtable:

```c
static char *my_voice_process_audio(TypioEngine *engine,
                                     const float *samples, size_t n_samples) {
    /* Run inference, return heap-allocated text or NULL */
}

static const TypioVoiceEngineOps my_voice_ops = {
    .process_audio = my_voice_process_audio,
};

static const TypioEngineBaseOps my_voice_base_ops = {
    .init = my_voice_init,
    .destroy = my_voice_destroy,
    .focus_in = my_voice_focus_in,
    .focus_out = my_voice_focus_out,
    .reset = my_voice_reset,
    .reload_config = my_voice_reload_config,
};

static TypioEngine *my_voice_create(void) {
    return typio_engine_new(&my_voice_info, &my_voice_base_ops,
                            NULL, &my_voice_ops);
}
```

## Build example

```cmake
cmake_minimum_required(VERSION 3.16)
project(typio-my-engine C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TYPIO REQUIRED typio)

add_library(typio-my-engine MODULE my_engine.c)
target_include_directories(typio-my-engine PRIVATE ${TYPIO_INCLUDE_DIRS})
target_link_libraries(typio-my-engine PRIVATE typio-core)

install(TARGETS typio-my-engine
    LIBRARY DESTINATION lib/typio/engines)
```

If `typio.pc` is in a non-standard prefix, export `PKG_CONFIG_PATH` before configuring.

## Install and verify

1. Install the engine into the Typio engine directory.
2. Confirm it appears:

```bash
typio --list
```

3. Run Typio with that engine:

```bash
typio --engine my-engine --verbose
```

## Practical guidance

### Base-ops checklist (all engines)

Every engine must provide all seven base callbacks.  If your engine does not need a particular behaviour, implement it as a no-op.

| Callback | Required | Reason |
|----------|----------|--------|
| `init` | **Yes** | Allocate `user_data`, load config |
| `destroy` | **Yes** | Free resources |
| `deactivate` | **Yes** | Free large resources when switched away (or no-op) |
| `focus_in` | **Yes** | Restore UI state (or no-op) |
| `focus_out` | **Yes** | Clear UI, preserve session state |
| `reset` | **Yes** | Cancel composition on Escape |
| `reload_config` | **Yes** | Re-parse config (or no-op) |

### Keyboard-ops checklist (keyboard engines only)

| Callback | Required | Reason |
|----------|----------|--------|
| `process_key` | **Yes** | Core input handling |
| `get_mode` | No | Sub-mode reporting (tray icon, popup) |
| `set_mode` | No | Mode switching via tray/D-Bus |

### Rules of thumb

- Keep engine state in `engine->user_data` or context properties.
- Do not block inside `process_key`.
- Return `TYPIO_KEY_NOT_HANDLED` for keys you want the compositor or client to keep.
- Use `typio_input_context_set_preedit()` and `typio_input_context_set_candidates()` only when your engine owns composition.
- The framework validates at registration time that keyboard engines provide `keyboard->process_key`.

## See also

- [Engine API Reference](../reference/api/engine.md)
- [Architecture Overview](../explanation/architecture-overview.md) — engine manager model
