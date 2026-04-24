/**
 * @file instance.c
 * @brief Typio instance implementation
 */


#include "typio/instance.h"
#include "typio/engine_manager.h"
#include "typio/input_context.h"
#include "typio/config.h"
#include "typio/config_schema.h"
#include "typio_build_config.h"
#include "../utils/log.h"
#include "../utils/string.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>

#define TYPIO_CONFIG_FILE_NAME "typio.toml"
#define TYPIO_RIME_STATE_FILE "rime-state.toml"
#define TYPIO_RIME_STATE_KEY "schema"

static void build_config_path(char *buffer, size_t buffer_size,
                              const char *config_dir, const char *file_name) {
    if (!buffer || buffer_size == 0 || !config_dir || !file_name) {
        return;
    }

    snprintf(buffer, buffer_size, "%s/%s", config_dir, file_name);
}

static char *build_state_path(TypioInstance *instance, const char *file_name) {
    const char *state_dir;
    size_t len;
    char *path;

    state_dir = typio_instance_get_state_dir(instance);
    if (!state_dir || !file_name) {
        return nullptr;
    }

    len = strlen(state_dir) + strlen(file_name) + 2;
    path = calloc(len, sizeof(char));
    if (!path) {
        return nullptr;
    }

    snprintf(path, len, "%s/%s", state_dir, file_name);
    return path;
}

static char *instance_dup_state_string(TypioInstance *instance,
                                       const char *file_name,
                                       const char *key) {
    TypioConfig *state;
    char *path;
    const char *value;
    char *copy = nullptr;

    if (!instance || !file_name || !key || !*key) {
        return nullptr;
    }

    path = build_state_path(instance, file_name);
    if (!path) {
        return nullptr;
    }

    state = typio_config_load_file(path);
    free(path);
    if (!state) {
        return nullptr;
    }

    value = typio_config_get_string(state, key, nullptr);
    if (value && *value) {
        copy = typio_strdup(value);
    }

    typio_config_free(state);
    return copy;
}

static TypioResult instance_set_state_string(TypioInstance *instance,
                                             const char *file_name,
                                             const char *key,
                                             const char *value) {
    TypioConfig *state;
    char *path;
    TypioResult result;

    if (!instance || !file_name || !key || !*key) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    path = build_state_path(instance, file_name);
    if (!path) {
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    state = typio_config_load_file(path);
    if (!state) {
        state = typio_config_new();
    }
    if (!state) {
        free(path);
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    if (value && *value) {
        typio_config_set_string(state, key, value);
    } else {
        typio_config_remove(state, key);
    }

    result = typio_config_save_file(state, path);
    typio_config_free(state);
    free(path);
    return result;
}

struct TypioInstance {
    TypioEngineManager *engine_manager;
    TypioConfig *config;

    char *config_dir;
    char *data_dir;
    char *state_dir;
    char *engine_dir;
    char *default_engine;

    TypioInputContext **contexts;
    size_t context_count;
    size_t context_capacity;
    TypioInputContext *focused_context;

    TypioEngineChangedCallback engine_changed_callback;
    void *engine_changed_user_data;
    TypioVoiceEngineChangedCallback voice_engine_changed_callback;
    void *voice_engine_changed_user_data;

    TypioStatusIconChangedCallback status_icon_changed_callback;
    void *status_icon_changed_user_data;
    char *last_status_icon;

    TypioModeChangedCallback mode_changed_callback;
    void *mode_changed_user_data;
    TypioEngineMode last_mode;
    bool has_mode;

    TypioLogCallback log_callback;
    void *log_user_data;

    bool rime_deploy_requested;
    bool initialized;
};

#ifdef BUILD_BASIC_ENGINE
extern const TypioEngineInfo *typio_engine_get_info_basic(void);
extern TypioEngine *typio_engine_create_basic(void);
#endif

static const char *get_default_config_dir(void) {
    static char path[512];
    const char *config_home = getenv("XDG_CONFIG_HOME");
    if (config_home && *config_home) {
        snprintf(path, sizeof(path), "%s/typio", config_home);
    } else {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(path, sizeof(path), "%s/.config/typio", home);
        } else {
            snprintf(path, sizeof(path), "/tmp/typio");
        }
    }
    return path;
}

static const char *get_default_data_dir(void) {
    static char path[512];
    const char *data_home = getenv("XDG_DATA_HOME");
    if (data_home && *data_home) {
        snprintf(path, sizeof(path), "%s/typio", data_home);
    } else {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(path, sizeof(path), "%s/.local/share/typio", home);
        } else {
            snprintf(path, sizeof(path), "/tmp/typio/data");
        }
    }
    return path;
}

static const char *get_default_state_dir(void) {
    static char path[512];
    const char *state_home = getenv("XDG_STATE_HOME");
    if (state_home && *state_home) {
        snprintf(path, sizeof(path), "%s/typio", state_home);
    } else {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(path, sizeof(path), "%s/.local/state/typio", home);
        } else {
            snprintf(path, sizeof(path), "/tmp/typio/state");
        }
    }
    return path;
}

static void ensure_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        mkdir(path, 0755);
    }
}

static void register_builtin_engines(TypioInstance *instance) {
    if (!instance || !instance->engine_manager) {
        return;
    }

#ifdef BUILD_BASIC_ENGINE
    TypioResult result = typio_engine_manager_register(
        instance->engine_manager,
        typio_engine_create_basic,
        typio_engine_get_info_basic);
    if (result != TYPIO_OK && result != TYPIO_ERROR_ALREADY_EXISTS) {
        typio_log_warning("Failed to register built-in basic engine");
    }
#endif
}

static TypioResult instance_ensure_config(TypioInstance *instance) {
    if (!instance) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    if (!instance->config) {
        instance->config = typio_config_new();
    }
    if (!instance->config) {
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    typio_config_apply_defaults(instance->config);
    return TYPIO_OK;
}

TypioInstance *typio_instance_new(void) {
    return typio_instance_new_with_config(nullptr);
}

TypioInstance *typio_instance_new_with_config(const TypioInstanceConfig *config) {
    TypioInstance *instance = calloc(1, sizeof(TypioInstance));
    if (!instance) {
        return nullptr;
    }

    /* Set directories */
    if (config && config->config_dir) {
        instance->config_dir = typio_strdup(config->config_dir);
    } else {
        instance->config_dir = typio_strdup(get_default_config_dir());
    }

    if (config && config->data_dir) {
        instance->data_dir = typio_strdup(config->data_dir);
    } else {
        instance->data_dir = typio_strdup(get_default_data_dir());
    }

    if (config && config->state_dir) {
        instance->state_dir = typio_strdup(config->state_dir);
    } else {
        instance->state_dir = typio_strdup(get_default_state_dir());
    }

    if (config && config->engine_dir) {
        instance->engine_dir = typio_strdup(config->engine_dir);
    } else {
        /* Default to TYPIO_ENGINE_DIR from config.h or the data-dir engine path */
#ifdef TYPIO_ENGINE_DIR
        instance->engine_dir = typio_strdup(TYPIO_ENGINE_DIR);
#else
        char engine_path[512];
        snprintf(engine_path, sizeof(engine_path), "%s/engines",
                 instance->data_dir);
        instance->engine_dir = typio_strdup(engine_path);
#endif
    }

    if (config && config->default_engine) {
        instance->default_engine = typio_strdup(config->default_engine);
    }

    if (config && config->log_callback) {
        instance->log_callback = config->log_callback;
        instance->log_user_data = config->log_user_data;
        typio_log_set_callback(config->log_callback, config->log_user_data);
    }

    /* Initialize context array */
    instance->context_capacity = 8;
    instance->contexts = calloc(instance->context_capacity,
                                sizeof(TypioInputContext *));
    if (!instance->contexts) {
        typio_instance_free(instance);
        return nullptr;
    }

    return instance;
}

void typio_instance_free(TypioInstance *instance) {
    if (!instance) {
        return;
    }

    if (instance->initialized) {
        typio_instance_shutdown(instance);
    }

    /* Free contexts */
    for (size_t i = 0; i < instance->context_count; i++) {
        typio_input_context_free(instance->contexts[i]);
    }
    free(instance->contexts);

    /* Free engine manager */
    if (instance->engine_manager) {
        typio_engine_manager_free(instance->engine_manager);
    }

    /* Free config */
    if (instance->config) {
        typio_config_free(instance->config);
    }

    free(instance->config_dir);
    free(instance->data_dir);
    free(instance->state_dir);
    free(instance->engine_dir);
    free(instance->default_engine);
    free(instance->last_status_icon);
    free((char *)instance->last_mode.mode_id);
    free((char *)instance->last_mode.display_label);
    free((char *)instance->last_mode.icon_name);
    free(instance);
}

TypioResult typio_instance_init(TypioInstance *instance) {
    TypioResult result;

    if (!instance) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    if (instance->initialized) {
        return TYPIO_OK;
    }

    typio_log_info("Initializing Typio instance");

    /* Ensure directories exist */
    ensure_directory(instance->config_dir);
    ensure_directory(instance->data_dir);
    ensure_directory(instance->state_dir);
    ensure_directory(instance->engine_dir);

    /* Load configuration */
    char config_path[512];
    build_config_path(config_path, sizeof(config_path),
                      instance->config_dir, TYPIO_CONFIG_FILE_NAME);

    instance->config = typio_config_load_file(config_path);
    if (!instance->config) {
        instance->config = typio_config_new();
    }
    result = instance_ensure_config(instance);
    if (result != TYPIO_OK) {
        typio_log_error("Failed to initialize configuration");
        return result;
    }

    /* Create engine manager */
    instance->engine_manager = typio_engine_manager_new(instance);
    if (!instance->engine_manager) {
        typio_log_error("Failed to create engine manager");
        return TYPIO_ERROR;
    }

    register_builtin_engines(instance);

    /* Load engines from engine directory */
    int loaded = typio_engine_manager_load_dir(instance->engine_manager,
                                                instance->engine_dir);
    typio_log_info("Loaded %d engines from %s", loaded, instance->engine_dir);

    /* Activate default engine if specified */
    const char *default_engine = instance->default_engine;
    if (!default_engine) {
        default_engine = typio_config_get_string(instance->config,
                                                  "default_engine", nullptr);
    }

    if (default_engine) {
        result = typio_engine_manager_set_active(instance->engine_manager,
                                                 default_engine);
        if (result != TYPIO_OK) {
            typio_log_warning("Failed to activate default engine: %s",
                              default_engine);
        }
    }

    if (!typio_engine_manager_get_active(instance->engine_manager)) {
        size_t count = 0;
        const char **engines = typio_engine_manager_list(instance->engine_manager, &count);
        if (engines && count > 0) {
            result = typio_engine_manager_set_active(instance->engine_manager,
                                                     engines[0]);
            if (result != TYPIO_OK) {
                typio_log_warning("Failed to activate first available engine: %s",
                                  engines[0]);
            }
        }
    }

    instance->initialized = true;
    typio_log_info("Typio instance initialized");

    return TYPIO_OK;
}

void typio_instance_shutdown(TypioInstance *instance) {
    if (!instance || !instance->initialized) {
        return;
    }

    typio_log_info("Shutting down Typio instance");

    /* Save configuration */
    typio_instance_save_config(instance);

    instance->initialized = false;
}

TypioEngineManager *typio_instance_get_engine_manager(TypioInstance *instance) {
    return instance ? instance->engine_manager : nullptr;
}

TypioInputContext *typio_instance_create_context(TypioInstance *instance) {
    if (!instance) {
        return nullptr;
    }

    TypioInputContext *ctx = typio_input_context_new(instance);
    if (!ctx) {
        return nullptr;
    }

    /* Grow array if needed */
    if (instance->context_count >= instance->context_capacity) {
        size_t new_capacity = instance->context_capacity * 2;
        TypioInputContext **new_contexts = realloc(
            instance->contexts,
            new_capacity * sizeof(TypioInputContext *));
        if (!new_contexts) {
            typio_input_context_free(ctx);
            return nullptr;
        }
        instance->contexts = new_contexts;
        instance->context_capacity = new_capacity;
    }

    instance->contexts[instance->context_count++] = ctx;

    return ctx;
}

void typio_instance_destroy_context(TypioInstance *instance,
                                     TypioInputContext *ctx) {
    if (!instance || !ctx) {
        return;
    }

    /* Remove from array */
    for (size_t i = 0; i < instance->context_count; i++) {
        if (instance->contexts[i] == ctx) {
            /* Shift remaining elements */
            memmove(&instance->contexts[i], &instance->contexts[i + 1],
                    (instance->context_count - i - 1) * sizeof(TypioInputContext *));
            instance->context_count--;
            break;
        }
    }

    /* Clear focused if this was focused */
    if (instance->focused_context == ctx) {
        instance->focused_context = nullptr;
    }

    typio_input_context_free(ctx);
}

TypioInputContext *typio_instance_get_focused_context(TypioInstance *instance) {
    return instance ? instance->focused_context : nullptr;
}

void typio_instance_set_engine_changed_callback(TypioInstance *instance,
                                                 TypioEngineChangedCallback callback,
                                                 void *user_data) {
    if (!instance) {
        return;
    }
    instance->engine_changed_callback = callback;
    instance->engine_changed_user_data = user_data;
}

void typio_instance_set_voice_engine_changed_callback(TypioInstance *instance,
                                                      TypioVoiceEngineChangedCallback callback,
                                                      void *user_data) {
    if (!instance) {
        return;
    }
    instance->voice_engine_changed_callback = callback;
    instance->voice_engine_changed_user_data = user_data;
}

void typio_instance_set_status_icon_changed_callback(TypioInstance *instance,
                                                      TypioStatusIconChangedCallback callback,
                                                      void *user_data) {
    if (!instance) {
        return;
    }
    instance->status_icon_changed_callback = callback;
    instance->status_icon_changed_user_data = user_data;
}

void typio_instance_notify_status_icon(TypioInstance *instance,
                                        const char *icon_name) {
    if (!instance || !icon_name) {
        return;
    }
    if (instance->last_status_icon &&
        strcmp(instance->last_status_icon, icon_name) == 0) {
        return;
    }
    free(instance->last_status_icon);
    instance->last_status_icon = strdup(icon_name);
    if (instance->status_icon_changed_callback) {
        instance->status_icon_changed_callback(instance, icon_name,
                                                instance->status_icon_changed_user_data);
    }
}

void typio_instance_clear_status_icon(TypioInstance *instance) {
    if (!instance || !instance->last_status_icon) {
        return;
    }

    free(instance->last_status_icon);
    instance->last_status_icon = nullptr;
}

const char *typio_instance_get_last_status_icon(TypioInstance *instance) {
    return instance ? instance->last_status_icon : nullptr;
}

void typio_instance_set_mode_changed_callback(TypioInstance *instance,
                                               TypioModeChangedCallback callback,
                                               void *user_data) {
    if (!instance) {
        return;
    }
    instance->mode_changed_callback = callback;
    instance->mode_changed_user_data = user_data;
}

static bool engine_mode_equal(const TypioEngineMode *a, const TypioEngineMode *b) {
    if (a->mode_class != b->mode_class) {
        return false;
    }
    if (a->mode_id != b->mode_id) {
        if (!a->mode_id || !b->mode_id || strcmp(a->mode_id, b->mode_id) != 0) {
            return false;
        }
    }
    return true;
}

static void engine_mode_store(TypioEngineMode *dst, const TypioEngineMode *src) {
    free((char *)dst->mode_id);
    free((char *)dst->display_label);
    free((char *)dst->icon_name);

    dst->mode_class = src->mode_class;
    dst->mode_id = src->mode_id ? strdup(src->mode_id) : nullptr;
    dst->display_label = src->display_label ? strdup(src->display_label) : nullptr;
    dst->icon_name = src->icon_name ? strdup(src->icon_name) : nullptr;
}

void typio_instance_notify_mode(TypioInstance *instance,
                                 const TypioEngineMode *mode) {
    if (!instance || !mode) {
        return;
    }
    if (instance->has_mode && engine_mode_equal(&instance->last_mode, mode)) {
        return;
    }

    engine_mode_store(&instance->last_mode, mode);
    instance->has_mode = true;

    /* Update legacy status icon from mode */
    if (mode->icon_name) {
        free(instance->last_status_icon);
        instance->last_status_icon = strdup(mode->icon_name);
    }

    if (instance->mode_changed_callback) {
        instance->mode_changed_callback(instance, &instance->last_mode,
                                         instance->mode_changed_user_data);
    }

    /* Also fire legacy icon callback for backward compat */
    if (instance->status_icon_changed_callback && mode->icon_name) {
        instance->status_icon_changed_callback(instance, mode->icon_name,
                                                instance->status_icon_changed_user_data);
    }
}

void typio_instance_clear_mode(TypioInstance *instance) {
    if (!instance || !instance->has_mode) {
        return;
    }

    free((char *)instance->last_mode.mode_id);
    free((char *)instance->last_mode.display_label);
    free((char *)instance->last_mode.icon_name);
    memset(&instance->last_mode, 0, sizeof(instance->last_mode));
    instance->has_mode = false;
}

const TypioEngineMode *typio_instance_get_last_mode(TypioInstance *instance) {
    if (!instance || !instance->has_mode) {
        return nullptr;
    }
    return &instance->last_mode;
}

const char *typio_instance_get_config_dir(TypioInstance *instance) {
    return instance ? instance->config_dir : nullptr;
}

const char *typio_instance_get_data_dir(TypioInstance *instance) {
    return instance ? instance->data_dir : nullptr;
}

const char *typio_instance_get_state_dir(TypioInstance *instance) {
    return instance ? instance->state_dir : nullptr;
}

TypioConfig *typio_instance_get_config(TypioInstance *instance) {
    return instance ? instance->config : nullptr;
}

TypioConfig *typio_instance_get_engine_config(TypioInstance *instance,
                                              const char *engine_name) {
    char section_name[256];

    if (!instance || !instance->config || !engine_name || !*engine_name) {
        return nullptr;
    }

    snprintf(section_name, sizeof(section_name), "engines.%s", engine_name);
    return typio_config_get_section(instance->config, section_name);
}

TypioResult typio_instance_reload_config(TypioInstance *instance) {
    if (!instance) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    char config_path[512];
    const char *configured_default_engine = nullptr;
    const char *current_engine_name = nullptr;
    TypioEngine *active;

    build_config_path(config_path, sizeof(config_path),
                      instance->config_dir, TYPIO_CONFIG_FILE_NAME);

    TypioConfig *new_config = typio_config_load_file(config_path);
    if (new_config) {
        typio_config_apply_defaults(new_config);
        if (instance->config) {
            typio_config_free(instance->config);
        }
        instance->config = new_config;
    }
    if (!instance->config) {
        instance->config = typio_config_new();
        if (!instance->config) {
            return TYPIO_ERROR_OUT_OF_MEMORY;
        }
        typio_config_apply_defaults(instance->config);
    }

    configured_default_engine = typio_config_get_string(instance->config,
                                                        "default_engine",
                                                        nullptr);
    active = typio_engine_manager_get_active(instance->engine_manager);
    current_engine_name = active ? typio_engine_get_name(active) : nullptr;
    if (configured_default_engine && *configured_default_engine &&
        (!current_engine_name ||
         strcmp(configured_default_engine, current_engine_name) != 0)) {
        typio_engine_manager_set_active(instance->engine_manager,
                                        configured_default_engine);
        active = typio_engine_manager_get_active(instance->engine_manager);
    }

    /* Notify keyboard engine to reload its config */
    if (active && active->ops->reload_config) {
        active->ops->reload_config(active);
    }

    /* Sync voice engine to match default_voice_engine config key */
    {
        const char *configured_voice = typio_config_get_string(
            instance->config, "default_voice_engine", nullptr);
        TypioEngine *active_voice =
            typio_engine_manager_get_active_voice(instance->engine_manager);
        const char *current_voice =
            active_voice ? typio_engine_get_name(active_voice) : nullptr;

        if (configured_voice && *configured_voice &&
            (!current_voice ||
             strcmp(configured_voice, current_voice) != 0)) {
            typio_engine_manager_set_active_voice(instance->engine_manager,
                                                   configured_voice);
        }
    }

    return TYPIO_OK;
}

TypioResult typio_instance_save_config(TypioInstance *instance) {
    if (!instance || !instance->config) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    char config_path[512];
    build_config_path(config_path, sizeof(config_path),
                      instance->config_dir, TYPIO_CONFIG_FILE_NAME);

    return typio_config_save_file(instance->config, config_path);
}

char *typio_instance_dup_rime_schema(TypioInstance *instance) {
    char *schema;
    const char *legacy_schema;

    if (!instance) {
        return nullptr;
    }

    schema = instance_dup_state_string(instance, TYPIO_RIME_STATE_FILE, TYPIO_RIME_STATE_KEY);
    if (schema) {
        return schema;
    }

    legacy_schema = instance->config
        ? typio_config_get_string(instance->config, "engines.rime.schema", nullptr)
        : nullptr;
    return (legacy_schema && *legacy_schema) ? typio_strdup(legacy_schema) : nullptr;
}

TypioResult typio_instance_set_rime_schema(TypioInstance *instance, const char *schema) {
    return instance_set_state_string(instance,
                                     TYPIO_RIME_STATE_FILE,
                                     TYPIO_RIME_STATE_KEY,
                                     schema);
}

TypioResult typio_instance_deploy_rime_config(TypioInstance *instance) {
    TypioEngine *rime;
    TypioResult result;

    if (!instance || !instance->engine_manager) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    rime = typio_engine_manager_get_engine(instance->engine_manager, "rime");
    if (!rime || !rime->ops || !rime->ops->reload_config) {
        return TYPIO_ERROR_NOT_FOUND;
    }

    if (!rime->initialized && rime->ops->init) {
        rime->instance = instance;
        result = rime->ops->init(rime, instance);
        if (result != TYPIO_OK) {
            return result;
        }
        rime->initialized = true;
    }

    instance->rime_deploy_requested = true;
    result = rime->ops->reload_config(rime);
    instance->rime_deploy_requested = false;
    return result;
}

char *typio_instance_get_config_text(TypioInstance *instance) {
    if (!instance || !instance->config) {
        return nullptr;
    }

    return typio_config_to_string(instance->config);
}

TypioResult typio_instance_set_config_text(TypioInstance *instance, const char *content) {
    TypioConfig *parsed;
    TypioConfig *old_config;
    TypioResult save_result;
    size_t old_key_count;
    size_t new_key_count;

    if (!instance || !content) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    parsed = typio_config_load_string(content);
    if (!parsed) {
        return TYPIO_ERROR;
    }

    old_key_count = instance->config ? typio_config_key_count(instance->config) : 0;
    new_key_count = typio_config_key_count(parsed);
    if (old_key_count > 0 && new_key_count == 0) {
        const unsigned char *p = (const unsigned char *)content;
        while (*p) {
            if (!isspace(*p) && *p != '#' && *p != ';') {
                break;
            }
            ++p;
        }
        typio_log_warning("Rejecting empty replacement config while existing config has %zu keys",
                          old_key_count);
        typio_config_free(parsed);
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    typio_config_apply_defaults(parsed);

    old_config = instance->config;
    instance->config = parsed;
    save_result = typio_instance_save_config(instance);
    if (save_result != TYPIO_OK) {
        instance->config = old_config;
        typio_config_free(parsed);
        return save_result;
    }
    typio_config_free(old_config);

    return typio_instance_reload_config(instance);
}

/* Internal function to notify engine change */
void typio_instance_notify_engine_changed(TypioInstance *instance,
                                           const TypioEngineInfo *engine) {
    if (instance && instance->engine_changed_callback) {
        instance->engine_changed_callback(instance, engine,
                                          instance->engine_changed_user_data);
    }
}

void typio_instance_notify_voice_engine_changed(TypioInstance *instance,
                                                const TypioEngineInfo *engine) {
    if (instance && instance->voice_engine_changed_callback) {
        instance->voice_engine_changed_callback(instance, engine,
                                                instance->voice_engine_changed_user_data);
    }
}

/* Internal function to set focused context */
void typio_instance_set_focused_context(TypioInstance *instance,
                                         TypioInputContext *ctx) {
    if (instance) {
        instance->focused_context = ctx;
    }
}

bool typio_instance_rime_deploy_requested(TypioInstance *instance) {
    return instance ? instance->rime_deploy_requested : false;
}
