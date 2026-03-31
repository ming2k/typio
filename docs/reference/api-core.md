# Core API Reference

This file summarizes the stable public surface under `src/core/include/typio/*.h`.

## `TypioInstance`

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
```

## `TypioInputContext`

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

void typio_input_context_set_surrounding(TypioInputContext *ctx, const char *text, int cursor_pos, int anchor_pos);
bool typio_input_context_get_surrounding(TypioInputContext *ctx, const char **text, int *cursor_pos, int *anchor_pos);

void typio_input_context_set_commit_callback(TypioInputContext *ctx, TypioCommitCallback callback, void *user_data);
void typio_input_context_set_preedit_callback(TypioInputContext *ctx, TypioPreeditCallback callback, void *user_data);
void typio_input_context_set_candidate_callback(TypioInputContext *ctx, TypioCandidateCallback callback, void *user_data);
```

## `TypioConfig`

TOML-compatible flat-key configuration storage.

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

## Events

```c
TypioKeyEvent *typio_key_event_new(TypioEventType type, uint32_t keycode, uint32_t keysym, uint32_t modifiers);
void typio_key_event_free(TypioKeyEvent *event);
uint32_t typio_key_event_get_unicode(const TypioKeyEvent *event);
bool typio_key_event_is_press(const TypioKeyEvent *event);
bool typio_key_event_is_release(const TypioKeyEvent *event);
```

For the full authoritative interface, read the installed headers under `include/typio/`.
