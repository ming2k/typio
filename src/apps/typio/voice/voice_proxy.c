/**
 * @file voice_proxy.c
 * @brief Reference-counted proxy implementation
 *
 * Single state machine shared by all voice engine adapters.
 *
 * Lifetimes
 * ---------
 * Three independent "in-flight" sources can hold the proxy alive past a
 * caller-visible destroy:
 *   1. process() — an inference call is using a backend it acquired.
 *   2. reload    — a background thread is loading a replacement backend.
 *   3. (proxy itself, not in-flight, but tracked by @c destroy_pending so
 *      the last one out finalizes the lock and frees memory).
 *
 * When destroy() arrives the proxy stops handing out backend references
 * (acquire fails) and parks the current @c impl in @c pending so the next
 * process() epilogue or reload_end frees it. The last in-flight party to
 * notice impl_refcount == 0 && !reload_running finalizes the proxy.
 *
 * Pending-backends slot
 * ---------------------
 * In realistic use at most 2 backends can be parked simultaneously (a
 * reload swap victim + a deactivate victim). The pending array is sized
 * generously and grown if ever needed; growth happens only when an old
 * pending backend cannot be freed yet because @c impl_refcount > 0,
 * which only matters if multiple swaps land while one process() call
 * runs — exceedingly rare in practice but bounded-correct.
 */

#include "voice_proxy.h"

#include <pthread.h>
#include <stdlib.h>

#define VOICE_PROXY_INITIAL_PENDING_CAP 4

struct TypioVoiceProxy {
    TypioVoiceBackend  base;
    pthread_mutex_t    lock;

    TypioVoiceBackend *impl;
    int                impl_refcount;
    bool               reload_running;
    bool               destroy_pending;

    /* Backends parked for the next refcount-drop. */
    TypioVoiceBackend **pending;
    int                 pending_count;
    int                 pending_cap;
};

/* ── pending list (under lock) ────────────────────────────────────────── */

/* Push @p b onto the pending list. On allocation failure returns false and
 * leaves the list untouched — caller MUST free @p b inline (no other path
 * will). All callers handle the failure by destroying the backend outside
 * the lock. */
static bool pending_push_locked(TypioVoiceProxy *p, TypioVoiceBackend *b) {
    if (!b) return true;
    if (p->pending_count == p->pending_cap) {
        int new_cap = p->pending_cap ? p->pending_cap * 2
                                     : VOICE_PROXY_INITIAL_PENDING_CAP;
        TypioVoiceBackend **np = realloc(p->pending,
                                          (size_t)new_cap * sizeof(*np));
        if (!np) return false;
        p->pending = np;
        p->pending_cap = new_cap;
    }
    p->pending[p->pending_count++] = b;
    return true;
}

/* Detach the pending list (caller frees outside the lock). */
static void pending_take_locked(TypioVoiceProxy *p,
                                 TypioVoiceBackend ***out_list,
                                 int *out_count) {
    *out_list = p->pending;
    *out_count = p->pending_count;
    p->pending = NULL;
    p->pending_count = 0;
    p->pending_cap = 0;
}

static void pending_free(TypioVoiceBackend **list, int count) {
    for (int i = 0; i < count; ++i) {
        if (list[i]) typio_voice_backend_destroy(list[i]);
    }
    free(list);
}

/* ── proxy backend ops ───────────────────────────────────────────────── */

static char *proxy_process(TypioVoiceBackend *backend,
                            const float *samples, size_t n_samples) {
    TypioVoiceProxy *p = (TypioVoiceProxy *)backend;
    TypioVoiceBackend *impl = NULL;

    /* Acquire: refuse new work after destroy has been requested so the
     * proxy can finalize promptly. */
    pthread_mutex_lock(&p->lock);
    if (!p->destroy_pending && p->impl) {
        impl = p->impl;
        p->impl_refcount++;
    }
    pthread_mutex_unlock(&p->lock);

    if (!impl) return NULL;

    /* Run inference unlocked. The impl we hold is guaranteed alive until
     * we drop refcount: a concurrent reload/deactivate/destroy will park
     * it in @c pending rather than free it. */
    char *result = typio_voice_backend_process(impl, samples, n_samples);

    /* Release. */
    TypioVoiceBackend **drain_list = NULL;
    int drain_count = 0;
    bool finalize = false;

    pthread_mutex_lock(&p->lock);
    p->impl_refcount--;
    if (p->impl_refcount == 0) {
        pending_take_locked(p, &drain_list, &drain_count);
        if (p->destroy_pending && !p->reload_running) {
            finalize = true;
        }
    }
    pthread_mutex_unlock(&p->lock);

    pending_free(drain_list, drain_count);
    if (finalize) {
        pthread_mutex_destroy(&p->lock);
        free(p);
    }
    return result;
}

static void proxy_destroy_via_backend(TypioVoiceBackend *backend) {
    typio_voice_proxy_destroy((TypioVoiceProxy *)backend);
}

static const TypioVoiceBackendOps proxy_ops = {
    .process = proxy_process,
    .destroy = proxy_destroy_via_backend,
};

/* ── public API ───────────────────────────────────────────────────────── */

TypioVoiceProxy *typio_voice_proxy_new(TypioVoiceBackend *initial_impl) {
    TypioVoiceProxy *p = calloc(1, sizeof(*p));
    if (!p) {
        if (initial_impl) typio_voice_backend_destroy(initial_impl);
        return NULL;
    }
    p->base.ops = &proxy_ops;
    if (pthread_mutex_init(&p->lock, NULL) != 0) {
        free(p);
        if (initial_impl) typio_voice_backend_destroy(initial_impl);
        return NULL;
    }
    p->impl = initial_impl;
    return p;
}

TypioVoiceBackend *typio_voice_proxy_as_backend(TypioVoiceProxy *p) {
    return p ? &p->base : NULL;
}

bool typio_voice_proxy_is_ready(TypioVoiceProxy *p) {
    if (!p) return false;
    pthread_mutex_lock(&p->lock);
    bool ready = !p->destroy_pending && p->impl != NULL;
    pthread_mutex_unlock(&p->lock);
    return ready;
}

/* Swap @c p->impl to @c new_impl. Returns the old impl (caller must
 * either park it via pending or destroy it inline depending on refcount). */
static void swap_impl_safely(TypioVoiceProxy *p, TypioVoiceBackend *new_impl,
                              TypioVoiceBackend **out_immediate_free) {
    *out_immediate_free = NULL;
    TypioVoiceBackend *old = p->impl;
    p->impl = new_impl;
    if (!old) return;
    if (p->impl_refcount > 0) {
        if (!pending_push_locked(p, old)) {
            /* OOM: best-effort, free inline. UAF possible only if a
             * concurrent process() call is using `old` — pathological
             * OOM, accept the risk over leaking the model memory. */
            *out_immediate_free = old;
        }
    } else {
        *out_immediate_free = old;
    }
}

void typio_voice_proxy_destroy(TypioVoiceProxy *p) {
    if (!p) return;

    TypioVoiceBackend *immediate_free = NULL;
    TypioVoiceBackend **drain_list = NULL;
    int drain_count = 0;
    bool finalize = false;

    pthread_mutex_lock(&p->lock);
    p->destroy_pending = true;
    swap_impl_safely(p, NULL, &immediate_free);
    if (p->impl_refcount == 0 && !p->reload_running) {
        pending_take_locked(p, &drain_list, &drain_count);
        finalize = true;
    }
    pthread_mutex_unlock(&p->lock);

    if (immediate_free) typio_voice_backend_destroy(immediate_free);
    pending_free(drain_list, drain_count);
    if (finalize) {
        pthread_mutex_destroy(&p->lock);
        free(p);
    }
}

void typio_voice_proxy_clear_impl(TypioVoiceProxy *p) {
    if (!p) return;

    TypioVoiceBackend *immediate_free = NULL;
    pthread_mutex_lock(&p->lock);
    if (!p->destroy_pending) {
        swap_impl_safely(p, NULL, &immediate_free);
    }
    pthread_mutex_unlock(&p->lock);

    if (immediate_free) typio_voice_backend_destroy(immediate_free);
}

void typio_voice_proxy_set_impl(TypioVoiceProxy *p,
                                 TypioVoiceBackend *new_impl) {
    if (!p) {
        if (new_impl) typio_voice_backend_destroy(new_impl);
        return;
    }

    TypioVoiceBackend *immediate_free = NULL;
    bool discard_new = false;

    pthread_mutex_lock(&p->lock);
    if (p->destroy_pending) {
        /* Late arrival; nobody to use it. */
        discard_new = true;
    } else {
        swap_impl_safely(p, new_impl, &immediate_free);
    }
    pthread_mutex_unlock(&p->lock);

    if (discard_new && new_impl) typio_voice_backend_destroy(new_impl);
    if (immediate_free) typio_voice_backend_destroy(immediate_free);
}

bool typio_voice_proxy_reload_begin(TypioVoiceProxy *p) {
    if (!p) return false;
    bool ok = false;
    pthread_mutex_lock(&p->lock);
    if (!p->destroy_pending && !p->reload_running) {
        p->reload_running = true;
        ok = true;
    }
    pthread_mutex_unlock(&p->lock);
    return ok;
}

void typio_voice_proxy_reload_end(TypioVoiceProxy *p,
                                   TypioVoiceBackend *new_impl) {
    if (!p) {
        if (new_impl) typio_voice_backend_destroy(new_impl);
        return;
    }

    TypioVoiceBackend *immediate_free = NULL;
    TypioVoiceBackend **drain_list = NULL;
    int drain_count = 0;
    bool finalize = false;
    bool discard_new = false;

    pthread_mutex_lock(&p->lock);
    p->reload_running = false;

    if (p->destroy_pending) {
        /* Engine was destroyed while we were loading; drop new_impl. */
        discard_new = true;
        if (p->impl_refcount == 0) {
            pending_take_locked(p, &drain_list, &drain_count);
            finalize = true;
        }
    } else {
        swap_impl_safely(p, new_impl, &immediate_free);
    }
    pthread_mutex_unlock(&p->lock);

    if (discard_new && new_impl) typio_voice_backend_destroy(new_impl);
    if (immediate_free) typio_voice_backend_destroy(immediate_free);
    pending_free(drain_list, drain_count);
    if (finalize) {
        pthread_mutex_destroy(&p->lock);
        free(p);
    }
}
