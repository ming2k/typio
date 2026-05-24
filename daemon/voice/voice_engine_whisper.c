/**
 * @file voice_engine_whisper.c
 * @brief Whisper voice engine adapter
 *
 * Wraps the whisper.cpp TypioVoiceBackend as a TYPIO_ENGINE_TYPE_VOICE engine,
 * so it can be managed by engine_manager with [engines.whisper] config.
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
} WhisperReloadArg;

static void whisper_reload_arg_free(WhisperReloadArg *a) {
    free(a->data_dir);
    free(a->language);
    free(a->model);
    free(a);
}

static void *whisper_reload_bg(void *arg) {
    WhisperReloadArg *a = arg;

    TypioVoiceBackend *new_impl =
        typio_voice_backend_whisper_new(a->data_dir, a->language, a->model);

    typio_voice_proxy_reload_end(a->proxy, new_impl);

    if (new_impl) {
        typio_log(TYPIO_LOG_INFO,
                  "Whisper: async reload complete, new model active");
    } else {
        typio_log(TYPIO_LOG_WARNING,
                  "Whisper: async reload complete, no backend available");
    }

    whisper_reload_arg_free(a);
    return NULL;
}

/* ── Engine implementation ──────────────────────────────────────────────── */

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
    .struct_size = TYPIO_ENGINE_INFO_SIZE,
};

static TypioResult whisper_engine_init(TypioEngine *engine,
                                        TypioInstance *instance) {
    const char *data_dir = typio_instance_get_data_dir(instance);
    const char *language = NULL;
    const char *model = "base";

    TypioConfig *ecfg = typio_instance_get_engine_config(instance, "whisper");
    if (ecfg) {
        const char *l = typio_config_get_string(ecfg, "language", NULL);
        const char *m = typio_config_get_string(ecfg, "model", NULL);
        if (l) language = l;
        if (m) model = m;
    }

    /* Load initial model synchronously — startup cost is acceptable. */
    TypioVoiceBackend *impl = typio_voice_backend_whisper_new(data_dir, language, model);
    if (!impl) {
        typio_log_warning("Whisper engine init: no backend available");
    }

    TypioVoiceProxy *proxy = typio_voice_proxy_new(impl);
    if (!proxy) return TYPIO_ERROR_OUT_OF_MEMORY;

    engine->user_data = typio_voice_proxy_as_backend(proxy);
    return TYPIO_OK;
}

static void whisper_engine_destroy(TypioEngine *engine) {
    if (engine->user_data) {
        /* This may return before the proxy is actually freed if process()
         * is in flight; the proxy finalizes itself when refcount drops. */
        typio_voice_backend_destroy(engine->user_data);
        engine->user_data = NULL;
    }
}

static void whisper_engine_deactivate(TypioEngine *engine) {
    TypioVoiceProxy *proxy = (TypioVoiceProxy *)engine->user_data;
    if (!proxy) return;
    typio_voice_proxy_clear_impl(proxy);
    typio_log(TYPIO_LOG_INFO, "Whisper: model freed on deactivate");
}

static void whisper_engine_focus_in(TypioEngine *engine,
                                     [[maybe_unused]] TypioInputContext *ctx) {
    TypioVoiceProxy *proxy = (TypioVoiceProxy *)engine->user_data;
    if (!proxy || typio_voice_proxy_is_ready(proxy)) return;

    const char *data_dir = typio_instance_get_data_dir(engine->instance);
    const char *language = NULL;
    const char *model = "base";

    TypioConfig *ecfg = typio_instance_get_engine_config(engine->instance, "whisper");
    if (ecfg) {
        const char *l = typio_config_get_string(ecfg, "language", NULL);
        const char *m = typio_config_get_string(ecfg, "model", NULL);
        if (l) language = l;
        if (m) model = m;
    }

    TypioVoiceBackend *impl = typio_voice_backend_whisper_new(data_dir, language, model);
    typio_voice_proxy_set_impl(proxy, impl);

    if (impl) {
        typio_log(TYPIO_LOG_INFO, "Whisper: model reloaded on focus_in");
    } else {
        typio_log(TYPIO_LOG_WARNING, "Whisper: failed to reload model on focus_in");
    }
}

static void whisper_engine_focus_out([[maybe_unused]] TypioEngine *engine,
                                      [[maybe_unused]] TypioInputContext *ctx) {
}

static void whisper_engine_reset([[maybe_unused]] TypioEngine *engine,
                                  [[maybe_unused]] TypioInputContext *ctx) {
}

static TypioResult whisper_engine_reload_config(TypioEngine *engine) {
    if (!engine || !engine->instance) return TYPIO_ERROR_INVALID_ARGUMENT;

    TypioVoiceProxy *proxy = (TypioVoiceProxy *)engine->user_data;
    if (!proxy) return TYPIO_ERROR_INVALID_ARGUMENT;

    if (!typio_voice_proxy_reload_begin(proxy)) {
        typio_log(TYPIO_LOG_INFO,
                  "Whisper: reload already in progress or proxy gone, skipping");
        return TYPIO_OK;
    }

    /* Snapshot config on the main thread before spawning. */
    const char *data_dir = typio_instance_get_data_dir(engine->instance);
    const char *language = NULL;
    const char *model    = "base";

    TypioConfig *ecfg =
        typio_instance_get_engine_config(engine->instance, "whisper");
    if (ecfg) {
        const char *l = typio_config_get_string(ecfg, "language", NULL);
        const char *m = typio_config_get_string(ecfg, "model", NULL);
        if (l) language = l;
        if (m) model = m;
    }

    WhisperReloadArg *arg = calloc(1, sizeof(WhisperReloadArg));
    if (!arg) {
        typio_voice_proxy_reload_end(proxy, NULL);
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    arg->proxy    = proxy;
    arg->data_dir = data_dir ? strdup(data_dir) : NULL;
    arg->language = language ? strdup(language) : NULL;
    arg->model    = strdup(model);

    typio_log(TYPIO_LOG_INFO, "Whisper: spawning async reload thread");

    pthread_t t;
    if (pthread_create(&t, NULL, whisper_reload_bg, arg) == 0) {
        pthread_detach(t);
    } else {
        typio_log(TYPIO_LOG_WARNING,
                  "Whisper: failed to spawn reload thread, reloading synchronously");
        whisper_reload_bg(arg); /* arg is freed inside */
    }

    return TYPIO_OK;
}

/* ── Voice ops ──────────────────────────────────────────────────────────── */

static bool whisper_engine_is_ready(TypioEngine *engine) {
    return engine && typio_voice_proxy_is_ready((TypioVoiceProxy *)engine->user_data);
}

static char *whisper_engine_process_audio(TypioEngine *engine,
                                           const float *samples, size_t n_samples) {
    if (!engine || !engine->user_data) return NULL;
    return typio_voice_backend_process((TypioVoiceBackend *)engine->user_data,
                                        samples, n_samples);
}

static const TypioVoiceEngineOps whisper_voice_ops = {
    .is_ready      = whisper_engine_is_ready,
    .process_audio = whisper_engine_process_audio,
};

static const TypioEngineBaseOps whisper_base_ops = {
    .init = whisper_engine_init,
    .destroy = whisper_engine_destroy,
    .deactivate = whisper_engine_deactivate,
    .focus_in = whisper_engine_focus_in,
    .focus_out = whisper_engine_focus_out,
    .reset = whisper_engine_reset,
    .reload_config = whisper_engine_reload_config,
};

const TypioEngineInfo *typio_engine_get_info_whisper(void) {
    return &whisper_engine_info;
}

TypioEngine *typio_engine_create_whisper(void) {
    return typio_engine_new(&whisper_engine_info, &whisper_base_ops, nullptr,
                            &whisper_voice_ops);
}
