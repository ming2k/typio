/**
 * @file test_resume_model.c
 * @brief Pure suspend-gap / fire-cooldown decision tests
 */

#include "resume_model.h"

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

TEST(no_gap_when_clocks_track_together) {
    uint64_t gap = 12345;
    /* A normal 100ms poll iteration: both clocks advance equally. */
    ASSERT(!typio_wl_resume_gap_exceeded(100, 100, 2000, &gap));
    ASSERT(gap == 0);
}

TEST(small_gap_is_below_threshold) {
    uint64_t gap = 0;
    /* 500ms of extra boottime (a debugger pause, brief freeze) must not
     * be mistaken for a suspend. */
    ASSERT(!typio_wl_resume_gap_exceeded(100, 600, 2000, &gap));
    ASSERT(gap == 500);
}

TEST(gap_at_threshold_fires) {
    uint64_t gap = 0;
    ASSERT(typio_wl_resume_gap_exceeded(100, 2100, 2000, &gap));
    ASSERT(gap == 2000);
}

TEST(large_suspend_fires) {
    uint64_t gap = 0;
    /* 30 minutes of suspend while monotonic barely moved. */
    ASSERT(typio_wl_resume_gap_exceeded(100, 1800000 + 100, 2000, &gap));
    ASSERT(gap == 1800000);
}

TEST(boot_behind_monotonic_never_fires) {
    uint64_t gap = 7;
    /* Defensive: boot delta can't legitimately trail monotonic, but if
     * it does (clock glitch) we must not underflow or fire. */
    ASSERT(!typio_wl_resume_gap_exceeded(200, 100, 2000, &gap));
    ASSERT(gap == 0);
}

TEST(gap_out_pointer_is_optional) {
    ASSERT(typio_wl_resume_gap_exceeded(0, 5000, 2000, NULL));
    ASSERT(!typio_wl_resume_gap_exceeded(100, 100, 2000, NULL));
}

TEST(cooldown_suppresses_recent_fire) {
    /* Fired at 1000, now at 3000, window 5000 -> still cooling down. */
    ASSERT(typio_wl_resume_in_cooldown(3000, 1000, 5000));
}

TEST(cooldown_expires_after_window) {
    /* Fired at 1000, now at 6500, window 5000 -> window elapsed. */
    ASSERT(!typio_wl_resume_in_cooldown(6500, 1000, 5000));
}

TEST(cooldown_inactive_before_first_fire) {
    /* last_fire == 0 means nothing has fired yet. */
    ASSERT(!typio_wl_resume_in_cooldown(9999, 0, 5000));
}

TEST(cooldown_boundary_is_exclusive) {
    /* Exactly at the window edge counts as expired. */
    ASSERT(!typio_wl_resume_in_cooldown(6000, 1000, 5000));
}

int main(void) {
    printf("Running resume model tests:\n");

    run_test_no_gap_when_clocks_track_together();
    run_test_small_gap_is_below_threshold();
    run_test_gap_at_threshold_fires();
    run_test_large_suspend_fires();
    run_test_boot_behind_monotonic_never_fires();
    run_test_gap_out_pointer_is_optional();
    run_test_cooldown_suppresses_recent_fire();
    run_test_cooldown_expires_after_window();
    run_test_cooldown_inactive_before_first_fire();
    run_test_cooldown_boundary_is_exclusive();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
