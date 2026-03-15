/**
 * @file voice_engine_sherpa.c
 * @brief Sherpa-ONNX voice engine adapter
 *
 * Wraps the sherpa-onnx TypioVoiceBackend as a TYPIO_ENGINE_TYPE_VOICE engine,
 * so it can be managed by engine_manager with [engines.sherpa-onnx] config.
 */

#include "typio_build_config.h"
#include "voice_engine.h"
#include "typio/instance.h"
#include "typio/config.h"
#include "utils/log.h"

#include <stdlib.h>
#include <string.h>

static const TypioEngineInfo sherpa_engine_info = {
    .name = "sherpa-onnx",
    .display_name = "Sherpa-ONNX",
    .description = "Speech-to-text via sherpa-onnx",
    .version = "1.0",
    .author = "Typio",
    .icon = NULL,
    .language = NULL,
    .type = TYPIO_ENGINE_TYPE_VOICE,
    .capabilities = TYPIO_CAP_VOICE_INPUT,
    .api_version = TYPIO_API_VERSION,
};

static TypioResult sherpa_engine_init(TypioEngine *engine,
                                       TypioInstance *instance) {
    const char *data_dir = typio_instance_get_data_dir(instance);
    const char *language = NULL;
    const char *model = NULL;

    /* Try new [engines.sherpa-onnx] config first */
    TypioConfig *ecfg = typio_instance_get_engine_config(instance, "sherpa-onnx");
    if (ecfg) {
        language = typio_config_get_string(ecfg, "language", NULL);
        model = typio_config_get_string(ecfg, "model", NULL);
    }

    /* Legacy fallback: [voice] section */
    if (!model) {
        TypioConfig *config = typio_instance_get_config(instance);
        if (config) {
            const char *backend = typio_config_get_string(config,
                                                           "voice.backend", NULL);
            if (backend && (strcmp(backend, "sherpa-onnx") == 0 ||
                            strcmp(backend, "sherpa") == 0)) {
                if (!language) {
                    language = typio_config_get_string(config,
                                                       "voice.language", NULL);
                }
                if (!model) {
                    model = typio_config_get_string(config,
                                                     "voice.model", NULL);
                }
            }
            if (model && !ecfg) {
                typio_log_warning("Sherpa-ONNX config from [voice] is "
                                  "deprecated; use [engines.sherpa-onnx] instead");
            }
        }
    }

    TypioVoiceBackend *backend = typio_voice_backend_sherpa_new(data_dir,
                                                                 language,
                                                                 model);
    engine->user_data = backend;

    if (!backend) {
        typio_log_warning("Sherpa-ONNX engine init: no backend available");
    }

    return TYPIO_OK;
}

static void sherpa_engine_destroy(TypioEngine *engine) {
    if (engine->user_data) {
        typio_voice_backend_destroy(engine->user_data);
        engine->user_data = NULL;
    }
}

static TypioKeyProcessResult sherpa_engine_process_key(
        [[maybe_unused]] TypioEngine *engine,
        [[maybe_unused]] TypioInputContext *ctx,
        [[maybe_unused]] const TypioKeyEvent *event) {
    return TYPIO_KEY_NOT_HANDLED;
}

static TypioResult sherpa_engine_reload_config(TypioEngine *engine) {
    if (!engine || !engine->instance) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    /* Destroy old backend */
    if (engine->user_data) {
        typio_voice_backend_destroy(engine->user_data);
        engine->user_data = NULL;
    }

    /* Re-init with fresh config */
    return sherpa_engine_init(engine, engine->instance);
}

static const TypioEngineOps sherpa_engine_ops = {
    .init = sherpa_engine_init,
    .destroy = sherpa_engine_destroy,
    .process_key = sherpa_engine_process_key,
    .reload_config = sherpa_engine_reload_config,
};

const TypioEngineInfo *typio_engine_get_info_sherpa(void) {
    return &sherpa_engine_info;
}

TypioEngine *typio_engine_create_sherpa(void) {
    return typio_engine_new(&sherpa_engine_info, &sherpa_engine_ops);
}
