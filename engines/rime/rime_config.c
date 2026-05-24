/**
 * @file rime_config.c
 * @brief Configuration loading and cleanup for the Rime engine
 */

#include "path_expand.h"
#include "rime_internal.h"

void typio_rime_free_config(TypioRimeConfig *config) {
    if (!config) {
        return;
    }

    free(config->schema);
    free(config->shared_data_dir);
    free(config->user_data_dir);
    memset(config, 0, sizeof(*config));
}

TypioResult typio_rime_load_config(TypioEngine *engine,
                                   TypioInstance *instance,
                                   TypioRimeConfig *config) {
    char *persisted_schema;
    const char *data_dir;
    TypioConfig *engine_config = nullptr;
    char *default_user_dir;

    if (!engine || !instance || !config) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    memset(config, 0, sizeof(*config));
    config->schema = typio_strdup(TYPIO_RIME_DEFAULT_SCHEMA);
    config->shared_data_dir = typio_strdup(TYPIO_RIME_SHARED_DATA_DIR);
    data_dir = typio_instance_get_data_dir(instance);
    default_user_dir = typio_path_join(data_dir, "rime");
    config->user_data_dir = default_user_dir;

    if (!config->schema || !config->shared_data_dir || !config->user_data_dir) {
        typio_rime_free_config(config);
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    persisted_schema = typio_instance_dup_rime_schema(instance);
    if (persisted_schema && *persisted_schema) {
        free(config->schema);
        config->schema = persisted_schema;
        persisted_schema = nullptr;
    }
    engine_config = typio_instance_get_engine_config(instance, "rime");
    if (!engine_config) {
        return TYPIO_OK;
    }

    const char *shared_data_dir = typio_config_get_string(engine_config, "shared_data_dir", nullptr);
    const char *user_data_dir = typio_config_get_string(engine_config, "user_data_dir", nullptr);
    if (shared_data_dir && *shared_data_dir) {
        char *expanded = typio_rime_expand_path(shared_data_dir);
        free(config->shared_data_dir);
        config->shared_data_dir = expanded;
    }
    if (user_data_dir && *user_data_dir) {
        char *expanded = typio_rime_expand_path(user_data_dir);
        free(config->user_data_dir);
        config->user_data_dir = expanded;
    }

    typio_config_free(engine_config);
    free(persisted_schema);

    if (!config->schema || !config->shared_data_dir || !config->user_data_dir) {
        typio_rime_free_config(config);
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    return TYPIO_OK;
}
