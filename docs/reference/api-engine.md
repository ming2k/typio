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

## Engine Operations

```c
typedef struct TypioEngineOps {
    TypioResult (*init)(TypioEngine *engine, TypioInstance *instance);
    void (*destroy)(TypioEngine *engine);

    void (*focus_in)(TypioEngine *engine, TypioInputContext *ctx);
    void (*focus_out)(TypioEngine *engine, TypioInputContext *ctx);
    void (*reset)(TypioEngine *engine, TypioInputContext *ctx);

    TypioKeyProcessResult (*process_key)(TypioEngine *engine,
                                         TypioInputContext *ctx,
                                         const TypioKeyEvent *event);

    TypioResult (*voice_start)(TypioEngine *engine, TypioInputContext *ctx);
    TypioResult (*voice_stop)(TypioEngine *engine, TypioInputContext *ctx);
    TypioResult (*voice_process)(TypioEngine *engine,
                                 TypioInputContext *ctx,
                                 const void *audio_data,
                                 size_t size,
                                 int sample_rate,
                                 int channels);

    TypioResult (*get_config)(TypioEngine *engine, TypioConfig **config);
    TypioResult (*set_config)(TypioEngine *engine, const TypioConfig *config);
    TypioResult (*reload_config)(TypioEngine *engine);
    const char *(*get_preedit)(TypioEngine *engine, TypioInputContext *ctx);
    TypioCandidateList *(*get_candidates)(TypioEngine *engine, TypioInputContext *ctx);
    const char *(*get_status_icon)(TypioEngine *engine, TypioInputContext *ctx);
} TypioEngineOps;
```

## Engine Utilities

```c
TypioEngine *typio_engine_new(const TypioEngineInfo *info, const TypioEngineOps *ops);
void typio_engine_free(TypioEngine *engine);

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
