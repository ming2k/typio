/**
 * @file test_voice_proxy.c
 * @brief Concurrency tests for the shared voice proxy
 *
 * Covers the four interactions that used to be UAFs in the per-engine
 * mirror copies of this state machine:
 *   - destroy() while process() is in flight
 *   - deactivate (clear_impl) while process() is in flight
 *   - process() during reload
 *   - destroy() during reload
 */

#include "../core/src/voice/voice_proxy.h"
#include "typio/log.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── Fake backend ────────────────────────────────────────────────────── */

typedef struct FakeBackend {
    TypioVoiceBackend base;
    int               id;
    /* Hold process() inside until release_gate is signalled. */
    pthread_mutex_t   gate_lock;
    pthread_cond_t    gate_cv;
    int               release_gate;     /* 0 = wait, 1 = go */
    atomic_int        in_process;       /* 1 while process is executing */
    atomic_int       *destroy_counter;  /* incremented on destroy (shared) */
} FakeBackend;

static char *fake_process(TypioVoiceBackend *backend,
                           [[maybe_unused]] const float *samples,
                           [[maybe_unused]] size_t n_samples) {
    FakeBackend *b = (FakeBackend *)backend;
    atomic_fetch_add(&b->in_process, 1);

    pthread_mutex_lock(&b->gate_lock);
    while (!b->release_gate) {
        pthread_cond_wait(&b->gate_cv, &b->gate_lock);
    }
    pthread_mutex_unlock(&b->gate_lock);

    atomic_fetch_sub(&b->in_process, 1);

    char *out = malloc(32);
    snprintf(out, 32, "fake-%d", b->id);
    return out;
}

static void fake_destroy(TypioVoiceBackend *backend) {
    FakeBackend *b = (FakeBackend *)backend;
    if (b->destroy_counter) atomic_fetch_add(b->destroy_counter, 1);
    pthread_mutex_destroy(&b->gate_lock);
    pthread_cond_destroy(&b->gate_cv);
    free(b);
}

static const TypioVoiceBackendOps fake_ops = {
    .process = fake_process,
    .destroy = fake_destroy,
};

static FakeBackend *fake_new(int id, atomic_int *destroy_counter) {
    FakeBackend *b = calloc(1, sizeof(*b));
    b->base.ops = &fake_ops;
    b->id = id;
    pthread_mutex_init(&b->gate_lock, NULL);
    pthread_cond_init(&b->gate_cv, NULL);
    b->release_gate = 1;  /* default: don't block */
    atomic_init(&b->in_process, 0);
    b->destroy_counter = destroy_counter;
    return b;
}

static void fake_block(FakeBackend *b) {
    pthread_mutex_lock(&b->gate_lock);
    b->release_gate = 0;
    pthread_mutex_unlock(&b->gate_lock);
}

static void fake_release(FakeBackend *b) {
    pthread_mutex_lock(&b->gate_lock);
    b->release_gate = 1;
    pthread_cond_broadcast(&b->gate_cv);
    pthread_mutex_unlock(&b->gate_lock);
}

/* Spin-wait helper bounded so a hung test fails instead of hanging CI. */
static void wait_until(atomic_int *flag, int target, const char *what) {
    for (int i = 0; i < 5000; ++i) {
        if (atomic_load(flag) == target) return;
        struct timespec ts = {0, 1000000};  /* 1ms */
        nanosleep(&ts, NULL);
    }
    typio_log_error("wait_until timeout: %s", what);
    abort();
}

/* ── Process-thread helper ───────────────────────────────────────────── */

typedef struct {
    TypioVoiceProxy *proxy;
    char            *result;
} ProcArg;

static void *run_process(void *p) {
    ProcArg *a = p;
    a->result = typio_voice_backend_process(
        typio_voice_proxy_as_backend(a->proxy), NULL, 0);
    return NULL;
}

/* ── Tests ───────────────────────────────────────────────────────────── */

/* Sanity: destroy with nothing in flight frees immediately. */
static void test_destroy_idle(void) {
    atomic_int destroyed = 0;
    FakeBackend *b = fake_new(1, &destroyed);
    TypioVoiceProxy *p = typio_voice_proxy_new(&b->base);
    assert(p);
    typio_voice_proxy_destroy(p);
    assert(atomic_load(&destroyed) == 1);
    printf("  test_destroy_idle: OK\n");
}

/* destroy() while process() is mid-call: proxy must defer; impl freed once
 * process returns. */
static void test_destroy_during_process(void) {
    atomic_int destroyed = 0;
    FakeBackend *b = fake_new(2, &destroyed);
    fake_block(b);

    TypioVoiceProxy *p = typio_voice_proxy_new(&b->base);
    ProcArg arg = { p, NULL };
    pthread_t t;
    pthread_create(&t, NULL, run_process, &arg);

    wait_until(&b->in_process, 1, "process to enter");

    /* Concurrent destroy — must not free b yet. */
    typio_voice_proxy_destroy(p);
    assert(atomic_load(&destroyed) == 0);

    /* Release the in-flight process; epilogue should free both b and proxy. */
    fake_release(b);
    pthread_join(t, NULL);
    free(arg.result);
    assert(atomic_load(&destroyed) == 1);
    printf("  test_destroy_during_process: OK\n");
}

/* clear_impl() (engine deactivate) while process() is mid-call: parks the
 * impl, frees on refcount drop. */
static void test_deactivate_during_process(void) {
    atomic_int destroyed = 0;
    FakeBackend *b = fake_new(3, &destroyed);
    fake_block(b);

    TypioVoiceProxy *p = typio_voice_proxy_new(&b->base);
    ProcArg arg = { p, NULL };
    pthread_t t;
    pthread_create(&t, NULL, run_process, &arg);

    wait_until(&b->in_process, 1, "process to enter");

    typio_voice_proxy_clear_impl(p);
    assert(atomic_load(&destroyed) == 0);
    /* New process call must see no backend. */
    assert(!typio_voice_proxy_is_ready(p));

    fake_release(b);
    pthread_join(t, NULL);
    free(arg.result);
    assert(atomic_load(&destroyed) == 1);

    /* Proxy itself is still alive — clear_impl is not destroy. */
    typio_voice_proxy_destroy(p);
    printf("  test_deactivate_during_process: OK\n");
}

/* reload_end swaps in a new impl while a process() call is still using the
 * old one. Old impl must survive until refcount drops. */
static void test_process_during_reload(void) {
    atomic_int destroyed = 0;
    FakeBackend *old = fake_new(4, &destroyed);
    fake_block(old);

    TypioVoiceProxy *p = typio_voice_proxy_new(&old->base);
    ProcArg arg = { p, NULL };
    pthread_t t;
    pthread_create(&t, NULL, run_process, &arg);

    wait_until(&old->in_process, 1, "process to enter");

    assert(typio_voice_proxy_reload_begin(p));
    FakeBackend *fresh = fake_new(5, &destroyed);
    typio_voice_proxy_reload_end(p, &fresh->base);

    /* Old impl is parked, not freed. */
    assert(atomic_load(&destroyed) == 0);
    /* New backend is now active and ready. */
    assert(typio_voice_proxy_is_ready(p));

    fake_release(old);
    pthread_join(t, NULL);
    free(arg.result);

    /* Old impl was freed when process refcount dropped to 0. */
    assert(atomic_load(&destroyed) == 1);

    typio_voice_proxy_destroy(p);  /* frees the new impl too */
    assert(atomic_load(&destroyed) == 2);
    printf("  test_process_during_reload: OK\n");
}

/* destroy() during reload: proxy must wait for reload_end before finalizing.
 * The new impl produced by the reload is discarded. */
static void test_destroy_during_reload(void) {
    atomic_int destroyed = 0;
    FakeBackend *old = fake_new(6, &destroyed);

    TypioVoiceProxy *p = typio_voice_proxy_new(&old->base);
    assert(typio_voice_proxy_reload_begin(p));

    typio_voice_proxy_destroy(p);
    /* Old impl is dropped immediately (no in-flight process). */
    assert(atomic_load(&destroyed) == 1);

    /* Now the bg "reload" returns with a fresh impl — must be discarded. */
    FakeBackend *fresh = fake_new(7, &destroyed);
    typio_voice_proxy_reload_end(p, &fresh->base);
    assert(atomic_load(&destroyed) == 2);  /* fresh dropped too */
    printf("  test_destroy_during_reload: OK\n");
}

/* set_impl after destroy must discard the new backend (no leak, no UAF). */
static void test_set_impl_after_destroy(void) {
    atomic_int destroyed = 0;
    FakeBackend *first = fake_new(8, &destroyed);
    TypioVoiceProxy *p = typio_voice_proxy_new(&first->base);

    /* Hold a refcount with a blocked process so destroy defers. */
    fake_block(first);
    ProcArg arg = { p, NULL };
    pthread_t t;
    pthread_create(&t, NULL, run_process, &arg);
    wait_until(&first->in_process, 1, "process to enter");

    typio_voice_proxy_destroy(p);

    /* A late set_impl arrives — proxy is in destroy_pending, must discard. */
    FakeBackend *late = fake_new(9, &destroyed);
    typio_voice_proxy_set_impl(p, &late->base);
    assert(atomic_load(&destroyed) == 1);  /* late freed inline */

    fake_release(first);
    pthread_join(t, NULL);
    free(arg.result);
    assert(atomic_load(&destroyed) == 2);  /* first freed by epilogue */
    printf("  test_set_impl_after_destroy: OK\n");
}

int main(void) {
    printf("test_voice_proxy:\n");
    test_destroy_idle();
    test_destroy_during_process();
    test_deactivate_during_process();
    test_process_during_reload();
    test_destroy_during_reload();
    test_set_impl_after_destroy();
    printf("test_voice_proxy: ALL PASSED\n");
    return 0;
}
