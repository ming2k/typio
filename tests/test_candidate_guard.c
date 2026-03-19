/**
 * @file test_candidate_guard.c
 * @brief Tests for candidate navigation passthrough guard
 */

#include "candidate_guard.h"
#include "typio/input_context.h"

#include <stdio.h>
#include <stdlib.h>
#include <xkbcommon/xkbcommon-keysyms.h>

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
    } while (0)

TEST(rejects_non_navigation_keys) {
    ASSERT(!typio_wl_candidate_guard_is_navigation_keysym(XKB_KEY_Return));
    ASSERT(!typio_wl_candidate_guard_is_navigation_keysym(XKB_KEY_space));
}

TEST(does_not_consume_without_candidates) {
    ASSERT(!typio_wl_candidate_guard_should_consume(NULL, XKB_KEY_Up));
}

TEST(consumes_arrow_keys_when_candidates_exist) {
    TypioCandidate candidate = {
        .text = "ni",
        .comment = "",
        .label = "1",
    };
    TypioCandidateList candidates = {
        .candidates = &candidate,
        .count = 1,
        .selected = 0,
    };
    TypioInputContext *ctx = typio_input_context_new(NULL);

    ASSERT(ctx != NULL);
    typio_input_context_set_candidates(ctx, &candidates);

    ASSERT(typio_wl_candidate_guard_should_consume(ctx, XKB_KEY_Up));
    ASSERT(typio_wl_candidate_guard_should_consume(ctx, XKB_KEY_Down));
    ASSERT(typio_wl_candidate_guard_should_consume(ctx, XKB_KEY_Left));
    ASSERT(typio_wl_candidate_guard_should_consume(ctx, XKB_KEY_Right));
    ASSERT(!typio_wl_candidate_guard_should_consume(ctx, XKB_KEY_Return));

    typio_input_context_free(ctx);
}

int main(void) {
    printf("Running candidate guard tests:\n");

    run_test_rejects_non_navigation_keys();
    run_test_does_not_consume_without_candidates();
    run_test_consumes_arrow_keys_when_candidates_exist();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
