/**
 * @file voice_engine_sherpa.c
 * @brief Sherpa-ONNX voice engine adapter
 *
 * Wraps the sherpa-onnx TypioVoiceBackend as a TYPIO_ENGINE_TYPE_VOICE engine,
 * so it can be managed by engine_manager with [engines.sherpa-onnx] config.
 *
 * Concurrency, refcounting, and deferred destruction of the backend live in
 * voice_proxy.c — this file is only the engine-ops glue and the async-reload
 * orchestration.
 */

#include "typio_build_config.h"
#include "voice_engine.h"
#include "voice_backend.h"
#include "voice_proxy.h"
#include "typio/instance.h"
#include "typio/config.h"
#include "typio/log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* ── Async reload ──────────────────────────────────────────────────────── */

typedef struct {
    TypioVoiceProxy *proxy;
    char *data_dir;
    char *language;
    char *model;
} SherpaReloadArg;

static void sherpa_reload_arg_free(SherpaReloadArg *a) {
    free(a->data_dir);
    free(a->language);
    free(a->model);
    free(a);
}

static void *sherpa_reload_bg(void *arg) {
    SherpaReloadArg *a = arg;

    TypioVoiceBackend *new_impl =
        typio_voice_backend_sherpa_new(a->data_dir, a->language, a->model);

    typio_voice_proxy_reload_end(a->proxy, new_impl);

    if (new_impl) {
        typio_log(TYPIO_LOG_INFO,
                  "Sherpa-ONNX: async reload complete, new model active");
    } else {
        typio_log(TYPIO_LOG_WARNING,
                  "Sherpa-ONNX: async reload complete, no backend available");
    }

    sherpa_reload_arg_free(a);
    return NULL;
}

/* ── Engine implementation ──────────────────────────────────────────────── */

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
    .struct_size = TYPIO_ENGINE_INFO_SIZE,
};

static TypioResult sherpa_engine_init(TypioEngine *engine,
                                       TypioInstance *instance) {
    const char *data_dir = typio_instance_get_data_dir(instance);
    const char *language = NULL;
    const char *model    = NULL;

    TypioConfig *ecfg = typio_instance_get_engine_config(instance, "sherpa-onnx");
    if (ecfg) {
        language = typio_config_get_string(ecfg, "language", NULL);
        model    = typio_config_get_string(ecfg, "model", NULL);
    }

    TypioVoiceBackend *impl = typio_voice_backend_sherpa_new(data_dir, language, model);
    if (!impl) {
        typio_log_warning("Sherpa-ONNX engine init: no backend available");
    }

    TypioVoiceProxy *proxy = typio_voice_proxy_new(impl);
    if (!proxy) return TYPIO_ERROR_OUT_OF_MEMORY;

    engine->user_data = typio_voice_proxy_as_backend(proxy);
    return TYPIO_OK;
}

static void sherpa_engine_destroy(TypioEngine *engine) {
    if (engine->user_data) {
        typio_voice_backend_destroy(engine->user_data);
        engine->user_data = NULL;
    }
}

static void sherpa_engine_deactivate(TypioEngine *engine) {
    TypioVoiceProxy *proxy = (TypioVoiceProxy *)engine->user_data;
    if (!proxy) return;
    typio_voice_proxy_clear_impl(proxy);
    typio_log(TYPIO_LOG_INFO, "Sherpa-ONNX: model freed on deactivate");
}

static void sherpa_engine_focus_in(TypioEngine *engine,
                                     [[maybe_unused]] TypioInputContext *ctx) {
    TypioVoiceProxy *proxy = (TypioVoiceProxy *)engine->user_data;
    if (!proxy || typio_voice_proxy_is_ready(proxy)) return;

    const char *data_dir = typio_instance_get_data_dir(engine->instance);
    const char *language = NULL;
    const char *model    = NULL;

    TypioConfig *ecfg = typio_instance_get_engine_config(engine->instance, "sherpa-onnx");
    if (ecfg) {
        language = typio_config_get_string(ecfg, "language", NULL);
        model    = typio_config_get_string(ecfg, "model", NULL);
    }

    TypioVoiceBackend *impl = typio_voice_backend_sherpa_new(data_dir, language, model);
    typio_voice_proxy_set_impl(proxy, impl);

    if (impl) {
        typio_log(TYPIO_LOG_INFO, "Sherpa-ONNX: model reloaded on focus_in");
    } else {
        typio_log(TYPIO_LOG_WARNING, "Sherpa-ONNX: failed to reload model on focus_in");
    }
}

static void sherpa_engine_focus_out([[maybe_unused]] TypioEngine *engine,
                                      [[maybe_unused]] TypioInputContext *ctx) {
}

static void sherpa_engine_reset([[maybe_unused]] TypioEngine *engine,
                                 [[maybe_unused]] TypioInputContext *ctx) {
}

static TypioResult sherpa_engine_reload_config(TypioEngine *engine) {
    if (!engine || !engine->instance) return TYPIO_ERROR_INVALID_ARGUMENT;

    TypioVoiceProxy *proxy = (TypioVoiceProxy *)engine->user_data;
    if (!proxy) return TYPIO_ERROR_INVALID_ARGUMENT;

    if (!typio_voice_proxy_reload_begin(proxy)) {
        typio_log(TYPIO_LOG_INFO,
                  "Sherpa-ONNX: reload already in progress or proxy gone, skipping");
        return TYPIO_OK;
    }

    const char *data_dir = typio_instance_get_data_dir(engine->instance);
    const char *language = NULL;
    const char *model    = NULL;

    TypioConfig *ecfg =
        typio_instance_get_engine_config(engine->instance, "sherpa-onnx");
    if (ecfg) {
        language = typio_config_get_string(ecfg, "language", NULL);
        model    = typio_config_get_string(ecfg, "model", NULL);
    }

    SherpaReloadArg *arg = calloc(1, sizeof(SherpaReloadArg));
    if (!arg) {
        typio_voice_proxy_reload_end(proxy, NULL);
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    arg->proxy    = proxy;
    arg->data_dir = data_dir ? strdup(data_dir) : NULL;
    arg->language = language ? strdup(language) : NULL;
    arg->model    = model    ? strdup(model)    : NULL;

    typio_log(TYPIO_LOG_INFO, "Sherpa-ONNX: spawning async reload thread");

    pthread_t t;
    if (pthread_create(&t, NULL, sherpa_reload_bg, arg) == 0) {
        pthread_detach(t);
    } else {
        typio_log(TYPIO_LOG_WARNING,
                  "Sherpa-ONNX: failed to spawn reload thread, reloading synchronously");
        sherpa_reload_bg(arg);
    }

    return TYPIO_OK;
}

/* ── Voice ops ──────────────────────────────────────────────────────────── */

static bool sherpa_engine_is_ready(TypioEngine *engine) {
    return engine && typio_voice_proxy_is_ready((TypioVoiceProxy *)engine->user_data);
}

static char *sherpa_engine_process_audio(TypioEngine *engine,
                                          const float *samples, size_t n_samples) {
    if (!engine || !engine->user_data) return NULL;
    return typio_voice_backend_process((TypioVoiceBackend *)engine->user_data,
                                        samples, n_samples);
}

static const TypioVoiceEngineOps sherpa_voice_ops = {
    .is_ready      = sherpa_engine_is_ready,
    .process_audio = sherpa_engine_process_audio,
};

static const TypioEngineBaseOps sherpa_base_ops = {
    .init = sherpa_engine_init,
    .destroy = sherpa_engine_destroy,
    .deactivate = sherpa_engine_deactivate,
    .focus_in = sherpa_engine_focus_in,
    .focus_out = sherpa_engine_focus_out,
    .reset = sherpa_engine_reset,
    .reload_config = sherpa_engine_reload_config,
};

const TypioEngineInfo *typio_engine_get_info_sherpa(void) {
    return &sherpa_engine_info;
}

TypioEngine *typio_engine_create_sherpa(void) {
    return typio_engine_new(&sherpa_engine_info, &sherpa_base_ops, nullptr,
                            &sherpa_voice_ops);
}
