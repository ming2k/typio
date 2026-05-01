# Engine API Reference

## Engine Manager

```c
TypioEngineManager *typio_engine_manager_new(TypioInstance *instance);
void typio_engine_manager_free(TypioEngineManager *manager);

int typio_engine_manager_load_dir(TypioEngineManager *manager, const char *path);
TypioResult typio_engine_manager_load(TypioEngineManager *manager, const char *path);
TypioResult typio_engine_manager_register(TypioEngineManager *manager,
                                          TypioEngineFactory factory,
                                          TypioEngineInfoFunc info_func);
TypioResult typio_engine_manager_unload(TypioEngineManager *manager, const char *name);

const char **typio_engine_manager_list(TypioEngineManager *manager, size_t *count);
const char **typio_engine_manager_list_by_type(TypioEngineManager *manager,
                                               TypioEngineType type,
                                               size_t *count);
const char **typio_engine_manager_list_ordered_keyboards(TypioEngineManager *manager,
                                                         size_t *count);
const TypioEngineInfo *typio_engine_manager_get_info(TypioEngineManager *manager, const char *name);
TypioEngine *typio_engine_manager_get_engine(TypioEngineManager *manager, const char *name);
TypioResult typio_engine_manager_set_active(TypioEngineManager *manager, const char *name);
TypioResult typio_engine_manager_set_active_voice(TypioEngineManager *manager, const char *name);
TypioEngine *typio_engine_manager_get_active(TypioEngineManager *manager);
TypioEngine *typio_engine_manager_get_active_voice(TypioEngineManager *manager);
TypioEngine *typio_engine_manager_get_active_by_type(TypioEngineManager *manager,
                                                     TypioEngineType type);
TypioResult typio_engine_manager_next(TypioEngineManager *manager);
TypioResult typio_engine_manager_prev(TypioEngineManager *manager);
```

## Engine Metadata

```c
struct TypioEngineInfo {
    const char *name;
    const char *display_name;
    const char *description;
    const char *version;
    const char *author;
    const char *icon;
    const char *language;
    TypioEngineType type;
    uint32_t capabilities;
    int api_version;
};
```

## Engine Mode

```c
typedef enum {
    TYPIO_MODE_CLASS_NATIVE = 0,
    TYPIO_MODE_CLASS_LATIN  = 1,
} TypioModeClass;

typedef struct TypioEngineMode {
    TypioModeClass mode_class;
    const char *mode_id;
    const char *display_label;
    const char *icon_name;
} TypioEngineMode;
```

Engines expose their sub-mode (e.g. Rime Chinese/Latin, Mozc Hiragana/Katakana) through `get_mode`. The framework observes mode state and displays it in the tray icon, candidate popup, and D-Bus interface. `mode_class` provides a coarse NATIVE vs LATIN classification; `mode_id` and `display_label` carry engine-specific detail.

## Engine Operations

Typio separates engine operations into two layers:

1. **`TypioEngineBaseOps`** — mandatory for **every** engine (keyboard and voice).
2. **`TypioKeyboardEngineOps`** — mandatory only for **keyboard** engines.

This separation enforces interface segregation: a voice engine cannot accidentally register a `process_key` callback, and the compiler (not a runtime NULL check) guarantees that a keyboard engine provides `process_key`.

```c
typedef struct TypioEngineBaseOps {
    TypioResult (*init)(TypioEngine *engine, TypioInstance *instance);
    void (*destroy)(TypioEngine *engine);
    void (*deactivate)(TypioEngine *engine);
    void (*focus_in)(TypioEngine *engine, TypioInputContext *ctx);
    void (*focus_out)(TypioEngine *engine, TypioInputContext *ctx);
    void (*reset)(TypioEngine *engine, TypioInputContext *ctx);
    TypioResult (*reload_config)(TypioEngine *engine);
} TypioEngineBaseOps;

typedef struct TypioKeyboardEngineOps {
    TypioKeyProcessResult (*process_key)(TypioEngine *engine,
                                         TypioInputContext *ctx,
                                         const TypioKeyEvent *event);
    const TypioEngineMode *(*get_mode)(TypioEngine *engine, TypioInputContext *ctx);
    TypioResult (*set_mode)(TypioEngine *engine,
                            TypioInputContext *ctx,
                            const char *mode_id);
} TypioKeyboardEngineOps;
```

### Base operations (all engines)

| Callback | When called | What the engine must do |
|----------|-------------|------------------------|
| `init` | Once before the engine becomes active | Allocate state (via `typio_engine_set_user_data`), load config |
| `destroy` | Once on unload / exit | Free every resource allocated by `init` |
| `focus_in` | Input context gains focus | Restore visible UI state (preedit, candidates). No-op if the engine has no per-context state. |
| `focus_out` | Input context loses focus | Clear visible composition UI, **preserve** session state (e.g. `ascii_mode`) |
| `reset` | Explicit reset (e.g. Escape) | Cancel active composition, restore default mode |
| `reload_config` | User edits config or issues reload | Re-parse engine-specific settings without restart. Return `TYPIO_OK` if the engine does not support runtime reload. |

All six callbacks are mandatory.  Engines that do not need a particular behaviour should supply a no-op (e.g. an empty function or a function that simply returns `TYPIO_OK`).

### Keyboard operations (keyboard engines only)

| Callback | Required? | When called | Use case |
|----------|-----------|-------------|----------|
| `process_key` | **Yes** | For every key event the framework routes to the engine | Return `HANDLED`/`COMPOSING`/`COMMITTED` if consumed, otherwise `NOT_HANDLED` |
| `get_mode` | No | After focus-in or engine switch | Report current sub-mode (Chinese, Latin, Hiragana, …) |
| `set_mode` | No | User requests mode change via tray/D-Bus | Switch to the requested mode |

The framework validates at registration time that a keyboard engine provides `process_key`.  `get_mode` and `set_mode` are optional.

## Engine Instance

```c
struct TypioEngine {
    const TypioEngineInfo *info;
    const TypioEngineBaseOps *base_ops;        /* Mandatory */
    const TypioKeyboardEngineOps *keyboard;    /* Keyboard engines only */
    const TypioVoiceEngineOps *voice;          /* Voice engines only */
    TypioInstance *instance;
    void *user_data;
    bool active;
    bool initialized;
    char *config_path;
};
```

## Lifecycle

```c
TypioEngine *typio_engine_new(const TypioEngineInfo *info,
                               const TypioEngineBaseOps *base_ops,
                               const TypioKeyboardEngineOps *keyboard,
                               const TypioVoiceEngineOps *voice);
void typio_engine_free(TypioEngine *engine);
```

`typio_engine_new` takes four arguments:
- `info` — engine metadata (must not be NULL)
- `base_ops` — mandatory base operations (must not be NULL)
- `keyboard` — keyboard vtable.  Must be non-NULL for keyboard engines; must be NULL for voice engines.
- `voice` — voice vtable.  Must be non-NULL for voice engines; must be NULL for keyboard engines.

## Utilities

```c
const char *typio_engine_get_name(const TypioEngine *engine);
TypioEngineType typio_engine_get_type(const TypioEngine *engine);
uint32_t typio_engine_get_capabilities(const TypioEngine *engine);
bool typio_engine_has_capability(const TypioEngine *engine, TypioEngineCapability cap);
bool typio_engine_is_active(const TypioEngine *engine);

const char *typio_engine_get_config_path(const TypioEngine *engine);
void typio_engine_set_config_path(TypioEngine *engine, const char *path);
void typio_engine_set_user_data(TypioEngine *engine, void *data);
void *typio_engine_get_user_data(const TypioEngine *engine);
```

## Plugin Entry Points

External shared engines must export:

```c
const TypioEngineInfo *typio_engine_get_info(void);
TypioEngine *typio_engine_create(void);
```

The helper macro `TYPIO_ENGINE_DEFINE(info_var, create_func)` can generate both symbols.
