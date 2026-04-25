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

## Minimal engine

```c
#include <typio/typio.h>

static TypioResult my_init(TypioEngine *engine, TypioInstance *instance) {
    (void)engine;
    (void)instance;
    return TYPIO_OK;
}

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

static const TypioEngineOps my_ops = {
    .init = my_init,
    .process_key = my_process_key,
};

static TypioEngine *my_create(void) {
    return typio_engine_new(&my_info, &my_ops);
}

TYPIO_ENGINE_DEFINE(my_info, my_create)
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

- Keep engine state in `engine->user_data` or context properties.
- Do not block inside `process_key`.
- Return `TYPIO_KEY_NOT_HANDLED` for keys you want the compositor or client to keep.
- Use `typio_input_context_set_preedit()` and `typio_input_context_set_candidates()` only when your engine owns composition.

## See also

- [Engine API Reference](../reference/api/engine.md)
- [Architecture Overview](../explanation/architecture-overview.md) — engine manager model
