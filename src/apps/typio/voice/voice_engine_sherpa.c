/**
 * @file voice_engine_sherpa.c
 * @brief Sherpa-ONNX voice engine adapter
 *
 * Wraps the sherpa-onnx TypioVoiceBackend as a TYPIO_ENGINE_TYPE_VOICE engine,
 * so it can be managed by engine_manager with [engines.sherpa-onnx] config.
 *
 * Config reloads are non-blocking: a background thread loads the new model
 * and hot-swaps it into the proxy.  The inference thread always gets a
 * reference-counted snapshot so destroy is deferred until the call returns.
 */

#include "typio_build_config.h"
#include "voice_engine.h"
#include "voice_backend.h"
#include "typio/instance.h"
#include "typio/config.h"
#include "utils/log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* ── Proxy ──────────────────────────────────────────────────────────────── */

/**
 * SherpaProxy mirrors the WhisperProxy design: a TypioVoiceBackend wrapper
 * that reference-counts the real SherpaBackend and allows the reload thread
 * to hot-swap the inner backend without blocking the main event loop.
 */
typedef struct {
    TypioVoiceBackend  base;             /* MUST be first — enables safe cast */
    pthread_mutex_t    lock;
    TypioVoiceBackend *impl;             /* current active backend, may be NULL */
    TypioVoiceBackend *pending_destroy;  /* waiting for impl_refcount to drop */
    int                impl_refcount;   /* process() calls in flight */
    bool               reload_running;
    bool               pending_proxy_free; /* destroy() called while reload runs */
} SherpaProxy;

static char *sherpa_proxy_process(TypioVoiceBackend *backend,
                                   const float *samples, size_t n_samples) {
    SherpaProxy *p = (SherpaProxy *)backend;

    pthread_mutex_lock(&p->lock);
    TypioVoiceBackend *impl = p->impl;
    if (impl) p->impl_refcount++;
    pthread_mutex_unlock(&p->lock);

    if (!impl) return NULL;

    char *result = typio_voice_backend_process(impl, samples, n_samples);

    pthread_mutex_lock(&p->lock);
    p->impl_refcount--;
    TypioVoiceBackend *to_destroy = NULL;
    if (p->impl_refcount == 0 && p->pending_destroy) {
        to_destroy = p->pending_destroy;
        p->pending_destroy = NULL;
    }
    pthread_mutex_unlock(&p->lock);

    if (to_destroy) typio_voice_backend_destroy(to_destroy);
    return result;
}

static void sherpa_proxy_destroy(TypioVoiceBackend *backend) {
    SherpaProxy *p = (SherpaProxy *)backend;

    pthread_mutex_lock(&p->lock);
    if (p->reload_running) {
        p->pending_proxy_free = true;
        if (p->impl) {
            typio_voice_backend_destroy(p->impl);
            p->impl = NULL;
        }
        pthread_mutex_unlock(&p->lock);
        return;
    }
    pthread_mutex_unlock(&p->lock);

    if (p->impl) typio_voice_backend_destroy(p->impl);
    if (p->pending_destroy) typio_voice_backend_destroy(p->pending_destroy);
    pthread_mutex_destroy(&p->lock);
    free(p);
}

static const TypioVoiceBackendOps sherpa_proxy_ops = {
    .process = sherpa_proxy_process,
    .destroy = sherpa_proxy_destroy,
};

/* ── Background reload ──────────────────────────────────────────────────── */

typedef struct {
    SherpaProxy *proxy;
    char *data_dir;  /* heap-allocated snapshot; freed by thread */
    char *language;  /* may be NULL */
    char *model;     /* may be NULL */
} SherpaReloadArg;

static void *sherpa_reload_bg(void *arg) {
    SherpaReloadArg *a = arg;

    TypioVoiceBackend *new_impl =
        typio_voice_backend_sherpa_new(a->data_dir, a->language, a->model);

    pthread_mutex_lock(&a->proxy->lock);

    bool should_free_proxy = a->proxy->pending_proxy_free;

    if (should_free_proxy) {
        if (new_impl) typio_voice_backend_destroy(new_impl);
        pthread_mutex_unlock(&a->proxy->lock);
        pthread_mutex_destroy(&a->proxy->lock);
        free(a->proxy);
    } else {
        TypioVoiceBackend *old = a->proxy->impl;
        a->proxy->impl = new_impl;

        TypioVoiceBackend *to_destroy = NULL;
        if (old) {
            if (a->proxy->impl_refcount > 0) {
                a->proxy->pending_destroy = old;
            } else {
                to_destroy = old;
            }
        }
        a->proxy->reload_running = false;
        pthread_mutex_unlock(&a->proxy->lock);

        if (to_destroy) typio_voice_backend_destroy(to_destroy);

        if (new_impl) {
            typio_log(TYPIO_LOG_INFO,
                      "Sherpa-ONNX: async reload complete, new model active");
        } else {
            typio_log(TYPIO_LOG_WARNING,
                      "Sherpa-ONNX: async reload complete, no backend available");
        }
    }

    free(a->data_dir);
    free(a->language);
    free(a->model);
    free(a);
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

    SherpaProxy *proxy = calloc(1, sizeof(SherpaProxy));
    if (!proxy) return TYPIO_ERROR_OUT_OF_MEMORY;

    proxy->base.ops = &sherpa_proxy_ops;
    pthread_mutex_init(&proxy->lock, NULL);

    proxy->impl = typio_voice_backend_sherpa_new(data_dir, language, model);
    if (!proxy->impl) {
        typio_log_warning("Sherpa-ONNX engine init: no backend available");
    }

    engine->user_data = proxy;
    return TYPIO_OK;
}

static void sherpa_engine_destroy(TypioEngine *engine) {
    if (engine->user_data) {
        typio_voice_backend_destroy(engine->user_data);
        engine->user_data = NULL;
    }
}

static void sherpa_engine_deactivate(TypioEngine *engine) {
    SherpaProxy *proxy = engine->user_data;
    if (!proxy) {
        return;
    }

    pthread_mutex_lock(&proxy->lock);
    if (proxy->impl) {
        typio_voice_backend_destroy(proxy->impl);
        proxy->impl = NULL;
    }
    pthread_mutex_unlock(&proxy->lock);

    typio_log(TYPIO_LOG_INFO, "Sherpa-ONNX: model freed on deactivate");
}

static void sherpa_engine_focus_in(TypioEngine *engine,
                                     [[maybe_unused]] TypioInputContext *ctx) {
    SherpaProxy *proxy = engine->user_data;
    if (!proxy || proxy->impl) {
        return;
    }

    /* Lazily reload the model if it was freed during deactivate */
    pthread_mutex_lock(&proxy->lock);
    if (proxy->impl || proxy->reload_running) {
        pthread_mutex_unlock(&proxy->lock);
        return;
    }

    const char *data_dir = typio_instance_get_data_dir(engine->instance);
    const char *language = NULL;
    const char *model    = NULL;

    TypioConfig *ecfg = typio_instance_get_engine_config(engine->instance, "sherpa-onnx");
    if (ecfg) {
        language = typio_config_get_string(ecfg, "language", NULL);
        model    = typio_config_get_string(ecfg, "model", NULL);
    }

    proxy->impl = typio_voice_backend_sherpa_new(data_dir, language, model);
    pthread_mutex_unlock(&proxy->lock);

    if (proxy->impl) {
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

    SherpaProxy *proxy = engine->user_data;
    if (!proxy) return TYPIO_ERROR_INVALID_ARGUMENT;

    pthread_mutex_lock(&proxy->lock);
    if (proxy->reload_running) {
        pthread_mutex_unlock(&proxy->lock);
        typio_log(TYPIO_LOG_INFO,
                  "Sherpa-ONNX: reload already in progress, skipping");
        return TYPIO_OK;
    }
    proxy->reload_running = true;
    pthread_mutex_unlock(&proxy->lock);

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
        pthread_mutex_lock(&proxy->lock);
        proxy->reload_running = false;
        pthread_mutex_unlock(&proxy->lock);
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

static char *sherpa_engine_process_audio(TypioEngine *engine,
                                          const float *samples, size_t n_samples) {
    if (!engine || !engine->user_data) {
        return NULL;
    }
    return sherpa_proxy_process((TypioVoiceBackend *)engine->user_data,
                                 samples, n_samples);
}

static const TypioVoiceEngineOps sherpa_voice_ops = {
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
