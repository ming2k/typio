# Creating Custom Engines

Typio engines are shared libraries that implement the public engine ABI from `typio/engine.h`.

## Required Exported Symbols

Your shared object must export:

```c
const TypioEngineInfo *typio_engine_get_info(void);
TypioEngine *typio_engine_create(void);
```

## Minimal Engine

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

## Configuration Path

Before activation, Typio assigns each engine a per-user config path:

```c
const char *path = typio_engine_get_config_path(engine);
```

Typical value:

```text
~/.config/typio/engines/my-engine.toml
```

## Build Example

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

If `typio.pc` is installed in a non-standard prefix, export `PKG_CONFIG_PATH` accordingly before configuring the engine project.

## Testing the Engine

1. Install the engine into the Typio engine directory.
2. Confirm it appears:

```bash
typio --list
```

3. Run Typio with that engine:

```bash
typio --engine my-engine --verbose
```

## Practical Guidance

- keep engine state in `engine->user_data` or context properties
- do not block inside `process_key`
- return `TYPIO_KEY_NOT_HANDLED` for keys you want the compositor or client path to keep
- use `typio_input_context_set_preedit()` and `typio_input_context_set_candidates()` only when your engine really owns composition
