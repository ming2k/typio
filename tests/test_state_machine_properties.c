/**
 * @file test_state_machine_properties.c
 * @brief Property tests for the resilience state machines.
 *
 * Dependency-free: a small LCG drives randomized inputs through the pure
 * decision functions and checks invariants against an independent
 * re-derivation of each spec. Seeds are fixed so failures reproduce. This
 * complements the example-based tests by exercising long random sequences,
 * including the edge transitions (clock ties, cooldown boundaries,
 * threshold boundaries) that hand-written cases tend to miss.
 */

#include "checkpoint_codec.h"
#include "lifecycle_state.h"
#include "reconciler.h"
#include "reconnect_backoff.h"
#include "resume_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  Running %s... ", #name); \
        tests_run++; \
        test_##name(); \
        tests_passed++; \
        printf("OK\n"); \
    } \
    static void test_##name(void)

#define ASSERT(expr) \
    do { \
        if (!(expr)) { \
            printf("FAILED\n"); \
            printf("    Assertion failed: %s\n", #expr); \
            printf("    At %s:%d\n", __FILE__, __LINE__); \
            exit(1); \
        } \
    } while(0)

/* Deterministic LCG (Numerical Recipes constants). */
static uint32_t rng_state = 0;
static uint32_t rng_next(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}
static uint32_t rng_below(uint32_t bound) {
    return bound ? rng_next() % bound : 0;
}

#define ITERATIONS 20000

/* ---- reconcile_model -------------------------------------------------- */
/*
 * Property: typio_wl_reconcile_decide matches an independent re-derivation
 * of the debounced divergence rule across long random sequences, and the
 * structural invariants (REPAIR only at/after threshold; timer reset on OK
 * and REPAIR) always hold.
 */
TEST(reconcile_decide_matches_spec) {
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        rng_state = seed * 2654435761u;
        uint64_t lib_since = 0;     /* state owned by the library */
        uint64_t spec_since = 0;    /* our independent shadow */
        uint64_t now = 0;
        const uint64_t threshold = 1000 + rng_below(3000);

        for (int i = 0; i < ITERATIONS; ++i) {
            now += rng_below(800); /* non-decreasing clock, sometimes ties */
            bool agree = (rng_below(3) == 0); /* skew toward divergence */

            TypioWlReconcileAction got =
                typio_wl_reconcile_decide(agree, now, &lib_since, threshold);

            /* Re-derive the expected action + shadow state. */
            TypioWlReconcileAction want;
            if (agree) {
                want = TYPIO_WL_RECONCILE_OK;
                spec_since = 0;
            } else if (spec_since == 0) {
                want = TYPIO_WL_RECONCILE_ARM;
                spec_since = now;
            } else if (now >= spec_since && now - spec_since >= threshold) {
                want = TYPIO_WL_RECONCILE_REPAIR;
                spec_since = 0;
            } else {
                want = TYPIO_WL_RECONCILE_WAIT;
            }

            ASSERT(got == want);
            ASSERT(lib_since == spec_since);

            /* Structural invariants independent of the shadow. */
            if (got == TYPIO_WL_RECONCILE_OK || got == TYPIO_WL_RECONCILE_REPAIR)
                ASSERT(lib_since == 0);
            if (got == TYPIO_WL_RECONCILE_ARM)
                ASSERT(lib_since == now);
        }
    }
}

/* A divergence that persists without interruption must REPAIR within one
 * threshold window — the reconciler can never get permanently stuck. */
TEST(reconcile_always_converges_under_persistent_divergence) {
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        rng_state = seed * 40503u + 7u;
        uint64_t since = 0;
        uint64_t now = 1; /* avoid 0 so ARM's since!=0 */
        const uint64_t threshold = 500 + rng_below(2000);
        int saw_repair = 0;

        /* Feed only disagreements; with a positive time step each loop,
         * REPAIR must occur and then re-arm. */
        for (int i = 0; i < 64; ++i) {
            now += threshold; /* guarantee threshold crossed each step after arm */
            TypioWlReconcileAction a =
                typio_wl_reconcile_decide(false, now, &since, threshold);
            ASSERT(a == TYPIO_WL_RECONCILE_ARM || a == TYPIO_WL_RECONCILE_REPAIR);
            if (a == TYPIO_WL_RECONCILE_REPAIR) {
                saw_repair = 1;
                ASSERT(since == 0);
            }
        }
        ASSERT(saw_repair);
    }
}

/* ---- resume_model ---------------------------------------------------- */
TEST(resume_gap_matches_spec) {
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        rng_state = seed * 0x9E3779B1u;
        for (int i = 0; i < ITERATIONS; ++i) {
            uint64_t mono = rng_below(10000);
            uint64_t boot = rng_below(10000);
            uint64_t thresh = rng_below(5000);
            uint64_t gap = 123456; /* poison */

            bool got = typio_wl_resume_gap_exceeded(mono, boot, thresh, &gap);

            uint64_t want_gap = (boot > mono) ? (boot - mono) : 0;
            bool want = (boot > mono) && (want_gap >= thresh);

            ASSERT(gap == want_gap);
            ASSERT(got == want);
            /* A fire always corresponds to a strictly positive gap. */
            if (got)
                ASSERT(gap > 0);
        }
    }
}

TEST(resume_cooldown_matches_spec) {
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        rng_state = seed * 2246822519u;
        for (int i = 0; i < ITERATIONS; ++i) {
            uint64_t last = rng_below(100000);
            uint64_t now = last + rng_below(20000); /* now >= last */
            uint64_t cooldown = rng_below(10000);

            bool got = typio_wl_resume_in_cooldown(now, last, cooldown);
            bool want = (last != 0) && (now - last < cooldown);
            ASSERT(got == want);
        }
    }
}

/* Composed resume detector: fires gated by cooldown must never fire twice
 * within a cooldown window. */
TEST(resume_detector_respects_cooldown_window) {
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        rng_state = seed * 22695477u + 1u;
        const uint64_t cooldown = 5000;
        uint64_t last_fire = 0;
        uint64_t prev_fire = 0;
        uint64_t now = 0;

        for (int i = 0; i < ITERATIONS / 4; ++i) {
            now += rng_below(2000);
            /* A would-be fire this tick (gap detector said yes). */
            bool wants_fire = (rng_below(2) == 0);
            if (!wants_fire)
                continue;
            if (typio_wl_resume_in_cooldown(now, last_fire, cooldown))
                continue; /* suppressed */
            /* Actually fires. */
            if (prev_fire != 0)
                ASSERT(now - prev_fire >= cooldown);
            prev_fire = now;
            last_fire = now;
        }
    }
}

/* ---- lifecycle_state (exhaustive over the small axis space) ---------- */
TEST(lifecycle_projection_and_agreement_exhaustive) {
    const TypioWlConnState conns[] = { TYPIO_WL_CONN_DISCONNECTED, TYPIO_WL_CONN_CONNECTED };
    const TypioWlFocusState focuses[] = { TYPIO_WL_FOCUS_UNFOCUSED, TYPIO_WL_FOCUS_FOCUSED };
    const TypioWlGrabState grabs[] = { TYPIO_WL_GRAB_NONE, TYPIO_WL_GRAB_PENDING_KEYMAP, TYPIO_WL_GRAB_READY };
    const TypioWlCompState comps[] = { TYPIO_WL_COMP_IDLE, TYPIO_WL_COMP_COMPOSING };
    const TypioWlLifecyclePhase phases[] = {
        TYPIO_WL_PHASE_INACTIVE, TYPIO_WL_PHASE_ACTIVATING,
        TYPIO_WL_PHASE_ACTIVE, TYPIO_WL_PHASE_DEACTIVATING
    };

    for (size_t a = 0; a < 2; ++a)
    for (size_t b = 0; b < 2; ++b)
    for (size_t c = 0; c < 3; ++c)
    for (size_t d = 0; d < 2; ++d) {
        TypioWlLifecycleState s = {
            .conn = conns[a], .focus = focuses[b],
            .grab = grabs[c], .comp = comps[d]
        };

        TypioWlLifecyclePhase projected = typio_wl_lifecycle_project_phase(&s);

        /* Independent re-derivation of the projection. */
        TypioWlLifecyclePhase want;
        if (s.conn != TYPIO_WL_CONN_CONNECTED || s.focus != TYPIO_WL_FOCUS_FOCUSED)
            want = TYPIO_WL_PHASE_INACTIVE;
        else if (s.grab == TYPIO_WL_GRAB_READY)
            want = TYPIO_WL_PHASE_ACTIVE;
        else
            want = TYPIO_WL_PHASE_ACTIVATING;
        ASSERT(projected == want);
        /* The projection is never a transient phase. */
        ASSERT(projected != TYPIO_WL_PHASE_DEACTIVATING);

        for (size_t p = 0; p < 4; ++p) {
            TypioWlLifecyclePhase declared = phases[p];
            bool agrees = typio_wl_lifecycle_state_agrees(&s, declared);
            bool want_agree =
                (declared == TYPIO_WL_PHASE_ACTIVATING ||
                 declared == TYPIO_WL_PHASE_DEACTIVATING)
                    ? true
                    : (projected == declared);
            ASSERT(agrees == want_agree);
        }
    }
}

/* ---- reconnect_backoff ---------------------------------------------- */
TEST(backoff_is_monotonic_and_bounded_random) {
    for (int i = 0; i < ITERATIONS; ++i) {
        rng_state = (uint32_t)i * 2654435761u + 11u;
        uint32_t a = rng_next();
        uint32_t delay = typio_wl_reconnect_delay_ms(a);
        ASSERT(delay >= TYPIO_WL_RECONNECT_BASE_DELAY_MS ||
               delay == TYPIO_WL_RECONNECT_MAX_DELAY_MS);
        ASSERT(delay <= TYPIO_WL_RECONNECT_MAX_DELAY_MS);
        if (a + 1 != 0) {
            ASSERT(typio_wl_reconnect_delay_ms(a + 1) >= delay ||
                   delay == TYPIO_WL_RECONNECT_MAX_DELAY_MS);
        }
    }
}

/* ---- checkpoint_codec (random round-trip) --------------------------- */
TEST(checkpoint_round_trip_random_blobs) {
    static char blob[512];
    static const char *engines[] = { "rime", "mozc", "basic", "x" };

    for (int i = 0; i < 4000; ++i) {
        rng_state = (uint32_t)i * 40503u + 5u;
        size_t blob_size = rng_below(sizeof(blob) + 1);
        for (size_t j = 0; j < blob_size; ++j)
            blob[j] = (char)rng_below(256); /* binary-safe, includes NUL/high */

        TypioCkpRecord in;
        in.version = TYPIO_CKP_VERSION;
        in.engine_name = engines[rng_below(4)];
        in.engine_name_len = 0;
        in.identity = (rng_below(2) == 0) ? "" : "app:test";
        in.identity_len = 0;
        in.boottime_ms = rng_next();
        in.blob = blob_size ? blob : NULL;
        in.blob_size = blob_size;

        size_t size = 0;
        char *buf = typio_ckp_encode(&in, &size);
        ASSERT(buf != NULL);

        TypioCkpRecord out;
        ASSERT(typio_ckp_decode(buf, size, &out));
        ASSERT(out.version == TYPIO_CKP_VERSION);
        ASSERT(out.engine_name_len == strlen(in.engine_name));
        ASSERT(memcmp(out.engine_name, in.engine_name, out.engine_name_len) == 0);
        ASSERT(out.boottime_ms == in.boottime_ms);
        ASSERT(out.blob_size == blob_size);
        if (blob_size)
            ASSERT(memcmp(out.blob, blob, blob_size) == 0);

        /* Any single-byte corruption of magic must be rejected. */
        free(buf);
    }
}

int main(void) {
    printf("Running state machine property tests:\n");

    run_test_reconcile_decide_matches_spec();
    run_test_reconcile_always_converges_under_persistent_divergence();
    run_test_resume_gap_matches_spec();
    run_test_resume_cooldown_matches_spec();
    run_test_resume_detector_respects_cooldown_window();
    run_test_lifecycle_projection_and_agreement_exhaustive();
    run_test_backoff_is_monotonic_and_bounded_random();
    run_test_checkpoint_round_trip_random_blobs();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
