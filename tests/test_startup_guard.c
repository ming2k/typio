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

TEST(ignores_stale_presses_during_short_window) {
    ASSERT(typio_wl_startup_guard_should_ignore_stale_press(1000, 1020));
    ASSERT(typio_wl_startup_guard_should_ignore_stale_press(
        1000, 1000 + TYPIO_WL_STARTUP_STALE_KEY_GUARD_MS));
}

TEST(allows_presses_after_stale_window) {
    ASSERT(!typio_wl_startup_guard_should_ignore_stale_press(
        1000, 1001 + TYPIO_WL_STARTUP_STALE_KEY_GUARD_MS));
}

TEST(cleans_up_orphan_releases_during_short_window) {
    ASSERT(typio_wl_startup_guard_should_cleanup_stale_release(1000, 1020));
    ASSERT(typio_wl_startup_guard_should_cleanup_stale_release(
        1000, 1000 + TYPIO_WL_STARTUP_STALE_KEY_GUARD_MS));
}

TEST(allows_orphan_releases_after_stale_window) {
    ASSERT(!typio_wl_startup_guard_should_cleanup_stale_release(
        1000, 1001 + TYPIO_WL_STARTUP_STALE_KEY_GUARD_MS));
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

TEST(classifies_stale_press_before_enter_guard) {
    ASSERT(typio_wl_startup_guard_classify_press(
        1000, 1020, TYPIO_KEY_Return, false, true) ==
        TYPIO_WL_STARTUP_SUPPRESS_STALE_KEY);
}

TEST(allows_non_enter_after_stale_window) {
    ASSERT(typio_wl_startup_guard_classify_press(
        1000, 1200, TYPIO_KEY_space, false, false) ==
        TYPIO_WL_STARTUP_SUPPRESS_NONE);
}

int main(void) {
    printf("Running startup guard tests:\n");
    run_test_ignores_stale_presses_during_short_window();
    run_test_allows_presses_after_stale_window();
    run_test_cleans_up_orphan_releases_during_short_window();
    run_test_allows_orphan_releases_after_stale_window();
    run_test_cleans_up_shortcut_orphan_releases_with_blocking_modifiers();
    run_test_does_not_treat_plain_releases_as_shortcut_orphan_cleanup();
    run_test_classifies_stale_press_before_enter_guard();
    run_test_allows_non_enter_after_stale_window();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
