/**
 * @file test_reconcile_model.c
 * @brief Pure reconcile-decision tests
 */

#include "reconciler.h"

#include <stdio.h>
#include <stdlib.h>

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

#define THRESH 2000ULL

TEST(agreement_clears_divergence) {
    uint64_t since = 12345;
    ASSERT(typio_wl_reconcile_decide(true, 50000, &since, THRESH)
           == TYPIO_WL_RECONCILE_OK);
    ASSERT(since == 0);
}

TEST(first_divergence_arms_timer) {
    uint64_t since = 0;
    ASSERT(typio_wl_reconcile_decide(false, 1000, &since, THRESH)
           == TYPIO_WL_RECONCILE_ARM);
    ASSERT(since == 1000);
}

TEST(divergence_within_threshold_waits) {
    uint64_t since = 1000;
    /* 1500ms later, still under the 2000ms threshold. */
    ASSERT(typio_wl_reconcile_decide(false, 2500, &since, THRESH)
           == TYPIO_WL_RECONCILE_WAIT);
    ASSERT(since == 1000); /* unchanged */
}

TEST(divergence_past_threshold_repairs) {
    uint64_t since = 1000;
    /* 2000ms later, exactly at threshold -> repair. */
    ASSERT(typio_wl_reconcile_decide(false, 3000, &since, THRESH)
           == TYPIO_WL_RECONCILE_REPAIR);
    ASSERT(since == 0); /* reset after firing */
}

TEST(repair_then_recovery_to_agreement) {
    uint64_t since = 0;
    /* Arm, wait, repair, then the repair fixes things -> OK. */
    ASSERT(typio_wl_reconcile_decide(false, 1000, &since, THRESH)
           == TYPIO_WL_RECONCILE_ARM);
    ASSERT(typio_wl_reconcile_decide(false, 2000, &since, THRESH)
           == TYPIO_WL_RECONCILE_WAIT);
    ASSERT(typio_wl_reconcile_decide(false, 3000, &since, THRESH)
           == TYPIO_WL_RECONCILE_REPAIR);
    ASSERT(typio_wl_reconcile_decide(true, 3100, &since, THRESH)
           == TYPIO_WL_RECONCILE_OK);
    ASSERT(since == 0);
}

TEST(transient_agreement_resets_divergence_window) {
    uint64_t since = 0;
    /* A flicker of divergence that resolves before threshold must reset,
     * so a later unrelated divergence starts a fresh window. */
    ASSERT(typio_wl_reconcile_decide(false, 1000, &since, THRESH)
           == TYPIO_WL_RECONCILE_ARM);
    ASSERT(typio_wl_reconcile_decide(true, 1500, &since, THRESH)
           == TYPIO_WL_RECONCILE_OK);
    ASSERT(since == 0);
    ASSERT(typio_wl_reconcile_decide(false, 10000, &since, THRESH)
           == TYPIO_WL_RECONCILE_ARM);
    ASSERT(since == 10000);
}

TEST(null_pointer_is_safe) {
    ASSERT(typio_wl_reconcile_decide(false, 1000, NULL, THRESH)
           == TYPIO_WL_RECONCILE_OK);
}

TEST(clock_regression_does_not_falsely_repair) {
    uint64_t since = 5000;
    /* now < since (monotonic glitch); must not underflow into repair. */
    ASSERT(typio_wl_reconcile_decide(false, 4000, &since, THRESH)
           == TYPIO_WL_RECONCILE_WAIT);
}

int main(void) {
    printf("Running reconcile model tests:\n");

    run_test_agreement_clears_divergence();
    run_test_first_divergence_arms_timer();
    run_test_divergence_within_threshold_waits();
    run_test_divergence_past_threshold_repairs();
    run_test_repair_then_recovery_to_agreement();
    run_test_transient_agreement_resets_divergence_window();
    run_test_null_pointer_is_safe();
    run_test_clock_regression_does_not_falsely_repair();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
