/**
 * @file voice_engine_whisper.c
 * @brief Whisper voice engine adapter
 *
 * Wraps the whisper.cpp TypioVoiceBackend as a TYPIO_ENGINE_TYPE_VOICE engine,
 * so it can be managed by engine_manager with [engines.whisper] config.
 */

#include "typio_build_config.h"
#include "voice_engine.h"
#include "typio/instance.h"
#include "typio/config.h"
#include "utils/log.h"

#include <stdlib.h>
#include <string.h>

static const TypioEngineInfo whisper_engine_info = {
    .name = "whisper",
    .display_name = "Whisper",
    .description = "Speech-to-text via whisper.cpp",
    .version = "1.0",
    .author = "Typio",
    .icon = NULL,
    .language = NULL,
    .type = TYPIO_ENGINE_TYPE_VOICE,
    .capabilities = TYPIO_CAP_VOICE_INPUT,
    .api_version = TYPIO_API_VERSION,
};

static TypioResult whisper_engine_init(TypioEngine *engine,
                                        TypioInstance *instance) {
    const char *data_dir = typio_instance_get_data_dir(instance);
    const char *language = NULL;
    const char *model = NULL;

    /* Try new [engines.whisper] config first */
    TypioConfig *ecfg = typio_instance_get_engine_config(instance, "whisper");
    if (ecfg) {
        language = typio_config_get_string(ecfg, "language", NULL);
        model = typio_config_get_string(ecfg, "model", NULL);
    }

    if (!model) {
        model = "base";
    }

    TypioVoiceBackend *backend = typio_voice_backend_whisper_new(data_dir,
                                                                  language,
                                                                  model);
    engine->user_data = backend;

    if (!backend) {
        typio_log_warning("Whisper engine init: no backend available");
    }

    return TYPIO_OK;
}

static void whisper_engine_destroy(TypioEngine *engine) {
    if (engine->user_data) {
        typio_voice_backend_destroy(engine->user_data);
        engine->user_data = NULL;
    }
}

static TypioKeyProcessResult whisper_engine_process_key(
        [[maybe_unused]] TypioEngine *engine,
        [[maybe_unused]] TypioInputContext *ctx,
        [[maybe_unused]] const TypioKeyEvent *event) {
    return TYPIO_KEY_NOT_HANDLED;
}

static TypioResult whisper_engine_reload_config(TypioEngine *engine) {
    if (!engine || !engine->instance) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    /* Destroy old backend */
    if (engine->user_data) {
        typio_voice_backend_destroy(engine->user_data);
        engine->user_data = NULL;
    }

    /* Re-init with fresh config */
    return whisper_engine_init(engine, engine->instance);
}

static const TypioEngineOps whisper_engine_ops = {
    .init = whisper_engine_init,
    .destroy = whisper_engine_destroy,
    .process_key = whisper_engine_process_key,
    .reload_config = whisper_engine_reload_config,
};

const TypioEngineInfo *typio_engine_get_info_whisper(void) {
    return &whisper_engine_info;
}

TypioEngine *typio_engine_create_whisper(void) {
    return typio_engine_new(&whisper_engine_info, &whisper_engine_ops);
}
