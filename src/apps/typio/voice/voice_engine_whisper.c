/**
 * @file voice_engine_whisper.c
 * @brief Whisper voice engine adapter
 *
 * Wraps the whisper.cpp TypioVoiceBackend as a TYPIO_ENGINE_TYPE_VOICE engine,
 * so it can be managed by engine_manager with [engines.whisper] config.
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
 * WhisperProxy sits between the engine and the real WhisperBackend.
 * It is itself a TypioVoiceBackend (base is the first field) so callers
 * that receive engine->user_data cast as TypioVoiceBackend * work unchanged.
 *
 * Thread-safety contract
 * ----------------------
 * - `lock` protects impl, pending_destroy, impl_refcount, reload_running,
 *   and pending_proxy_free.
 * - process() increments impl_refcount under lock before using impl, then
 *   decrements it afterwards and defers impl destruction if needed.
 * - The reload thread swaps impl under lock; if impl_refcount > 0 at that
 *   moment it parks the old impl in pending_destroy rather than freeing it.
 * - destroy() sets pending_proxy_free if a reload is still running so the
 *   background thread frees the proxy when it finishes.
 */
typedef struct {
    TypioVoiceBackend  base;             /* MUST be first — enables safe cast */
    pthread_mutex_t    lock;
    TypioVoiceBackend *impl;             /* current active backend, may be NULL */
    TypioVoiceBackend *pending_destroy;  /* waiting for impl_refcount to drop */
    int                impl_refcount;   /* process() calls in flight */
    bool               reload_running;
    bool               pending_proxy_free; /* destroy() called while reload runs */
} WhisperProxy;

static char *whisper_proxy_process(TypioVoiceBackend *backend,
                                    const float *samples, size_t n_samples) {
    WhisperProxy *p = (WhisperProxy *)backend;

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

static void whisper_proxy_destroy(TypioVoiceBackend *backend) {
    WhisperProxy *p = (WhisperProxy *)backend;

    pthread_mutex_lock(&p->lock);
    if (p->reload_running) {
        /* Reload thread still holds a pointer to this proxy; let it free us. */
        p->pending_proxy_free = true;
        /* Destroy the current impl now so the proxy doesn't keep the model
         * alive past engine teardown — the new impl being loaded will be
         * dropped by the reload thread once it sees pending_proxy_free. */
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

static const TypioVoiceBackendOps whisper_proxy_ops = {
    .process = whisper_proxy_process,
    .destroy = whisper_proxy_destroy,
};

/* ── Background reload ──────────────────────────────────────────────────── */

typedef struct {
    WhisperProxy *proxy;
    char *data_dir;  /* heap-allocated snapshot; freed by thread */
    char *language;  /* may be NULL */
    char *model;
} WhisperReloadArg;

static void *whisper_reload_bg(void *arg) {
    WhisperReloadArg *a = arg;

    TypioVoiceBackend *new_impl =
        typio_voice_backend_whisper_new(a->data_dir, a->language, a->model);

    pthread_mutex_lock(&a->proxy->lock);

    bool should_free_proxy = a->proxy->pending_proxy_free;

    if (should_free_proxy) {
        /* Engine was destroyed while we were loading; discard the new model. */
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
                      "Whisper: async reload complete, new model active");
        } else {
            typio_log(TYPIO_LOG_WARNING,
                      "Whisper: async reload complete, no backend available");
        }
    }

    free(a->data_dir);
    free(a->language);
    free(a->model);
    free(a);
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

    WhisperProxy *proxy = calloc(1, sizeof(WhisperProxy));
    if (!proxy) return TYPIO_ERROR_OUT_OF_MEMORY;

    proxy->base.ops = &whisper_proxy_ops;
    pthread_mutex_init(&proxy->lock, NULL);

    /* Load initial model synchronously — startup cost is acceptable. */
    proxy->impl = typio_voice_backend_whisper_new(data_dir, language, model);
    if (!proxy->impl) {
        typio_log_warning("Whisper engine init: no backend available");
    }

    engine->user_data = proxy;
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
    if (!engine || !engine->instance) return TYPIO_ERROR_INVALID_ARGUMENT;

    WhisperProxy *proxy = engine->user_data;
    if (!proxy) return TYPIO_ERROR_INVALID_ARGUMENT;

    pthread_mutex_lock(&proxy->lock);
    if (proxy->reload_running) {
        pthread_mutex_unlock(&proxy->lock);
        typio_log(TYPIO_LOG_INFO,
                  "Whisper: reload already in progress, skipping");
        return TYPIO_OK;
    }
    proxy->reload_running = true;
    pthread_mutex_unlock(&proxy->lock);

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
        pthread_mutex_lock(&proxy->lock);
        proxy->reload_running = false;
        pthread_mutex_unlock(&proxy->lock);
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
