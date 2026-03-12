/**
 * @file engine.c
 * @brief Input engine implementation
 */

#include "typio/engine.h"
#include "../utils/log.h"
#include "../utils/string.h"

#include <stdlib.h>
#include <string.h>

TypioEngine *typio_engine_new(const TypioEngineInfo *info,
                               const TypioEngineOps *ops) {
    if (!info || !ops) {
        return NULL;
    }

    TypioEngine *engine = calloc(1, sizeof(TypioEngine));
    if (!engine) {
        return NULL;
    }

    engine->info = info;
    engine->ops = ops;
    engine->active = false;

    return engine;
}

void typio_engine_free(TypioEngine *engine) {
    if (!engine) {
        return;
    }

    if (engine->initialized && engine->ops && engine->ops->destroy) {
        engine->ops->destroy(engine);
        engine->initialized = false;
    }

    free(engine->config_path);
    free(engine);
}

const char *typio_engine_get_name(const TypioEngine *engine) {
    return (engine && engine->info) ? engine->info->name : NULL;
}

TypioEngineType typio_engine_get_type(const TypioEngine *engine) {
    return (engine && engine->info) ? engine->info->type : TYPIO_ENGINE_TYPE_KEYBOARD;
}

uint32_t typio_engine_get_capabilities(const TypioEngine *engine) {
    return (engine && engine->info) ? engine->info->capabilities : 0;
}

bool typio_engine_has_capability(const TypioEngine *engine,
                                  TypioEngineCapability cap) {
    if (!engine || !engine->info) {
        return false;
    }
    return (engine->info->capabilities & cap) != 0;
}

bool typio_engine_is_active(const TypioEngine *engine) {
    return engine ? engine->active : false;
}

void typio_engine_set_user_data(TypioEngine *engine, void *data) {
    if (engine) {
        engine->user_data = data;
    }
}

void *typio_engine_get_user_data(const TypioEngine *engine) {
    return engine ? engine->user_data : NULL;
}

const char *typio_engine_get_config_path(const TypioEngine *engine) {
    return engine ? engine->config_path : NULL;
}

void typio_engine_set_config_path(TypioEngine *engine, const char *path) {
    if (!engine) {
        return;
    }
    free(engine->config_path);
    engine->config_path = path ? typio_strdup(path) : NULL;
}

/* Internal function to activate engine */
TypioResult typio_engine_activate(TypioEngine *engine, TypioInstance *instance) {
    if (!engine) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    if (engine->active) {
        return TYPIO_OK;
    }

    engine->instance = instance;

    if (!engine->initialized && engine->ops && engine->ops->init) {
        TypioResult result = engine->ops->init(engine, instance);
        if (result != TYPIO_OK) {
            typio_log_error("Failed to initialize engine: %s",
                            engine->info->name);
            return result;
        }
        engine->initialized = true;
    }

    engine->active = true;
    typio_log_info("Engine activated: %s", engine->info->name);

    return TYPIO_OK;
}

/* Internal function to deactivate engine */
void typio_engine_deactivate(TypioEngine *engine) {
    if (!engine || !engine->active) {
        return;
    }

    engine->active = false;
    typio_log_info("Engine deactivated: %s", engine->info->name);
}
