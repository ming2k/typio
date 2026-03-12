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

TEST(ignores_enter_during_startup_window) {
    ASSERT(typio_wl_startup_guard_should_ignore_enter(
        1000, 1500, TYPIO_KEY_Return));
    ASSERT(typio_wl_startup_guard_should_ignore_enter(
        1000, 1500, TYPIO_KEY_KP_Enter));
}

TEST(allows_enter_after_startup_window) {
    ASSERT(!typio_wl_startup_guard_should_ignore_enter(
        1000, 2201, TYPIO_KEY_Return));
}

TEST(allows_other_keys) {
    ASSERT(!typio_wl_startup_guard_should_ignore_enter(
        1000, 1500, TYPIO_KEY_space));
}

TEST(tracks_startup_enter_until_release) {
    bool suppressed[8] = {false};
    size_t suppressed_count = 0;
    bool suppress_stale_keys = true;

    ASSERT(typio_wl_startup_guard_track_press(
        suppressed, 8, &suppressed_count, suppress_stale_keys,
        1000, 1200, 3, TYPIO_KEY_Return));
    ASSERT(suppressed[3]);
    ASSERT(suppressed_count == 1);

    ASSERT(typio_wl_startup_guard_track_press(
        suppressed, 8, &suppressed_count, suppress_stale_keys,
        1000, 2500, 3, TYPIO_KEY_Return));
    ASSERT(suppressed_count == 1);

    ASSERT(typio_wl_startup_guard_track_release(
        suppressed, 8, &suppressed_count, &suppress_stale_keys, 3));
    ASSERT(!suppressed[3]);
    ASSERT(suppressed_count == 0);
    ASSERT(!suppress_stale_keys);
}

TEST(release_clears_empty_stale_mode) {
    bool suppressed[8] = {false};
    size_t suppressed_count = 0;
    bool suppress_stale_keys = true;

    ASSERT(typio_wl_startup_guard_track_release(
        suppressed, 8, &suppressed_count, &suppress_stale_keys, 4));
    ASSERT(!suppress_stale_keys);
}

int main(void) {
    printf("Running startup guard tests:\n");
    run_test_ignores_enter_during_startup_window();
    run_test_allows_enter_after_startup_window();
    run_test_allows_other_keys();
    run_test_tracks_startup_enter_until_release();
    run_test_release_clears_empty_stale_mode();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
