#include "boundary_bridge.h"
#include "startup_guard.h"
#include "typio/event.h"

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
    } while (0)

TEST(guard_window_same_epoch) {
    ASSERT(typio_wl_startup_guard_is_in_guard_window(10, 10));
}

TEST(guard_window_epoch_plus_one) {
    ASSERT(typio_wl_startup_guard_is_in_guard_window(10, 11));
}

TEST(guard_window_epoch_plus_two) {
    ASSERT(typio_wl_startup_guard_is_in_guard_window(10, 12));
}

TEST(guard_window_epoch_plus_three_outside) {
    ASSERT(!typio_wl_startup_guard_is_in_guard_window(10, 13));
}

TEST(guard_window_current_before_created) {
    ASSERT(!typio_wl_startup_guard_is_in_guard_window(10, 5));
}

TEST(classify_suppresses_stale_in_window) {
    ASSERT(typio_wl_startup_guard_classify_press(10, 11, true) ==
           TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY);
}

TEST(classify_allows_after_window) {
    ASSERT(typio_wl_startup_guard_classify_press(10, 13, true) ==
           TYPIO_WL_STARTUP_SUPPRESS_NONE);
}

TEST(classify_allows_when_suppress_disabled) {
    ASSERT(typio_wl_startup_guard_classify_press(10, 11, false) ==
           TYPIO_WL_STARTUP_SUPPRESS_NONE);
}

TEST(cleans_up_shortcut_orphan_releases_with_blocking_modifiers) {
    ASSERT(typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_space,
        TYPIO_MOD_CTRL, false));
    ASSERT(typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_space,
        TYPIO_MOD_ALT, false));
    ASSERT(typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_space,
        TYPIO_MOD_SUPER, false));
}

TEST(does_not_treat_plain_releases_as_shortcut_orphan_cleanup) {
    ASSERT(!typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_space,
        TYPIO_MOD_NONE, false));
    ASSERT(!typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_space,
        TYPIO_MOD_SHIFT, false));
}

int main(void) {
    printf("Running startup guard tests:\n");
    run_test_guard_window_same_epoch();
    run_test_guard_window_epoch_plus_one();
    run_test_guard_window_epoch_plus_two();
    run_test_guard_window_epoch_plus_three_outside();
    run_test_guard_window_current_before_created();
    run_test_classify_suppresses_stale_in_window();
    run_test_classify_allows_after_window();
    run_test_classify_allows_when_suppress_disabled();
    run_test_cleans_up_shortcut_orphan_releases_with_blocking_modifiers();
    run_test_does_not_treat_plain_releases_as_shortcut_orphan_cleanup();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
