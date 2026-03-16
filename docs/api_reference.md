# API Reference

This document summarizes the main public C surface under `src/lib/typio/*.h`.
It is an overview, not a replacement for the installed headers.

## Core Lifecycle

### `TypioInstance`

Main lifecycle and ownership object.

```c
typedef struct TypioInstanceConfig {
    const char *config_dir;
    const char *data_dir;
    const char *engine_dir;
    const char *default_engine;
    TypioLogCallback log_callback;
    void *log_user_data;
} TypioInstanceConfig;

TypioInstance *typio_instance_new(void);
TypioInstance *typio_instance_new_with_config(const TypioInstanceConfig *config);
void typio_instance_free(TypioInstance *instance);
TypioResult typio_instance_init(TypioInstance *instance);
void typio_instance_shutdown(TypioInstance *instance);

TypioEngineManager *typio_instance_get_engine_manager(TypioInstance *instance);
TypioInputContext *typio_instance_create_context(TypioInstance *instance);
void typio_instance_destroy_context(TypioInstance *instance, TypioInputContext *ctx);
TypioInputContext *typio_instance_get_focused_context(TypioInstance *instance);

const char *typio_instance_get_config_dir(TypioInstance *instance);
const char *typio_instance_get_data_dir(TypioInstance *instance);

TypioResult typio_instance_reload_config(TypioInstance *instance);
TypioResult typio_instance_save_config(TypioInstance *instance);
char *typio_instance_get_config_text(TypioInstance *instance);
TypioResult typio_instance_set_config_text(TypioInstance *instance, const char *content);

void typio_instance_set_config_reloaded_callback(TypioInstance *instance,
                                                 TypioConfigReloadedCallback callback,
                                                 void *user_data);
```

`TypioConfigReloadedCallback` fires after config reload succeeds and the active
keyboard engine has been resynchronized. Subsystems such as shortcuts, voice,
and the status bus use this hook to stay aligned.

### `TypioInputContext`

Per-client input state and callback surface.

```c
void typio_input_context_focus_in(TypioInputContext *ctx);
void typio_input_context_focus_out(TypioInputContext *ctx);
bool typio_input_context_is_focused(TypioInputContext *ctx);
void typio_input_context_reset(TypioInputContext *ctx);
bool typio_input_context_process_key(TypioInputContext *ctx, const TypioKeyEvent *event);

void typio_input_context_commit(TypioInputContext *ctx, const char *text);

void typio_input_context_set_preedit(TypioInputContext *ctx, const TypioPreedit *preedit);
const TypioPreedit *typio_input_context_get_preedit(TypioInputContext *ctx);
void typio_input_context_clear_preedit(TypioInputContext *ctx);

void typio_input_context_set_candidates(TypioInputContext *ctx, const TypioCandidateList *candidates);
const TypioCandidateList *typio_input_context_get_candidates(TypioInputContext *ctx);
void typio_input_context_clear_candidates(TypioInputContext *ctx);

void typio_input_context_set_surrounding(TypioInputContext *ctx,
                                         const char *text,
                                         int cursor_pos,
                                         int anchor_pos);
bool typio_input_context_get_surrounding(TypioInputContext *ctx,
                                         const char **text,
                                         int *cursor_pos,
                                         int *anchor_pos);
```

## Config API

`TypioConfig` stores TOML-compatible flat keys such as `default_engine` or
`engines.rime.schema`.

```c
TypioConfig *typio_config_new(void);
TypioConfig *typio_config_load_file(const char *path);
TypioConfig *typio_config_load_string(const char *content);
void typio_config_free(TypioConfig *config);

TypioResult typio_config_save_file(const TypioConfig *config, const char *path);
char *typio_config_to_string(const TypioConfig *config);

const char *typio_config_get_string(const TypioConfig *config, const char *key, const char *default_val);
int typio_config_get_int(const TypioConfig *config, const char *key, int default_val);
bool typio_config_get_bool(const TypioConfig *config, const char *key, bool default_val);
double typio_config_get_float(const TypioConfig *config, const char *key, double default_val);

TypioResult typio_config_set_string(TypioConfig *config, const char *key, const char *value);
TypioResult typio_config_set_int(TypioConfig *config, const char *key, int value);
TypioResult typio_config_set_bool(TypioConfig *config, const char *key, bool value);
TypioResult typio_config_set_float(TypioConfig *config, const char *key, double value);
```

Common helpers not shown above include section accessors such as
`typio_config_get_section()` and schema/default application helpers.

## Engine System

### `TypioEngineManager`

Loads engine metadata, creates instances lazily, and tracks active keyboard and
voice engines.

```c
TypioEngineManager *typio_engine_manager_new(TypioInstance *instance);
void typio_engine_manager_free(TypioEngineManager *manager);

int typio_engine_manager_load_dir(TypioEngineManager *manager, const char *path);
TypioResult typio_engine_manager_load(TypioEngineManager *manager, const char *path);
TypioResult typio_engine_manager_register(TypioEngineManager *manager,
                                          TypioEngineFactory factory,
                                          TypioEngineInfoFunc info_func);

const char **typio_engine_manager_list(TypioEngineManager *manager, size_t *count);
const TypioEngineInfo *typio_engine_manager_get_info(TypioEngineManager *manager, const char *name);
TypioEngine *typio_engine_manager_get_engine(TypioEngineManager *manager, const char *name);

TypioResult typio_engine_manager_set_active(TypioEngineManager *manager, const char *name);
TypioResult typio_engine_manager_set_active_voice(TypioEngineManager *manager, const char *name);
TypioEngine *typio_engine_manager_get_active(TypioEngineManager *manager);
TypioEngine *typio_engine_manager_get_active_voice(TypioEngineManager *manager);
```

### `TypioEngineInfo`

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

### `TypioEngineOps`

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

    bool (*select_candidate)(TypioEngine *engine, TypioInputContext *ctx, int index);
    bool (*page_candidates)(TypioEngine *engine, TypioInputContext *ctx, bool next);

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
} TypioEngineOps;
```

### Plugin Entry Points

External shared engines must export:

```c
const TypioEngineInfo *typio_engine_get_info(void);
TypioEngine *typio_engine_create(void);
```

The helper macro `TYPIO_ENGINE_DEFINE(info_var, create_func)` can generate both
symbols.

## Events

```c
TypioKeyEvent *typio_key_event_new(TypioEventType type,
                                   uint32_t keycode,
                                   uint32_t keysym,
                                   uint32_t modifiers);
void typio_key_event_free(TypioKeyEvent *event);
uint32_t typio_key_event_get_unicode(const TypioKeyEvent *event);
bool typio_key_event_is_press(const TypioKeyEvent *event);
bool typio_key_event_is_release(const TypioKeyEvent *event);
```

## Authoritative Source

For exact signatures and comments, read the public headers under
`include/typio/` or `src/lib/typio/`.
