/**
 * @file test_key_tracking.c
 * @brief Key tracking lifecycle helper tests
 */

#include "key_tracking.h"

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

TEST(reset_clears_all_states) {
    TypioKeyTrackState states[7] = {
        TYPIO_KEY_TRACK_FORWARDED,
        TYPIO_KEY_TRACK_BASIC_PASSTHROUGH,
        TYPIO_KEY_TRACK_APP_SHORTCUT,
        TYPIO_KEY_TRACK_RELEASED_PENDING,
        TYPIO_KEY_TRACK_SUPPRESSED_STARTUP,
        TYPIO_KEY_TRACK_IDLE,
        TYPIO_KEY_TRACK_IDLE,
    };

    typio_wl_key_tracking_reset(states, 7);

    for (size_t i = 0; i < 7; ++i)
        ASSERT(states[i] == TYPIO_KEY_TRACK_IDLE);
}

TEST(reset_clears_all_generations) {
    uint32_t generations[4] = { 9, 3, 1, 7 };

    typio_wl_key_tracking_reset_generations(generations, 4);

    for (size_t i = 0; i < 4; ++i)
        ASSERT(generations[i] == 0);
}

TEST(mark_released_pending_only_changes_forwarded_keys) {
    TypioKeyTrackState states[7] = {
        TYPIO_KEY_TRACK_IDLE,
        TYPIO_KEY_TRACK_FORWARDED,
        TYPIO_KEY_TRACK_BASIC_PASSTHROUGH,
        TYPIO_KEY_TRACK_APP_SHORTCUT,
        TYPIO_KEY_TRACK_SUPPRESSED_STARTUP,
        TYPIO_KEY_TRACK_FORWARDED,
        TYPIO_KEY_TRACK_IDLE,
    };

    ASSERT(typio_wl_key_tracking_mark_released_pending(states, 7) == 4);
    ASSERT(states[0] == TYPIO_KEY_TRACK_IDLE);
    ASSERT(states[1] == TYPIO_KEY_TRACK_RELEASED_PENDING);
    ASSERT(states[2] == TYPIO_KEY_TRACK_RELEASED_PENDING);
    ASSERT(states[3] == TYPIO_KEY_TRACK_RELEASED_PENDING);
    ASSERT(states[4] == TYPIO_KEY_TRACK_SUPPRESSED_STARTUP);
    ASSERT(states[5] == TYPIO_KEY_TRACK_RELEASED_PENDING);
    ASSERT(states[6] == TYPIO_KEY_TRACK_IDLE);
}

TEST(ctrl_shortcut_invariant_keeps_forwarded_key_until_boundary_cleanup) {
    TypioKeyTrackState states[4] = {
        TYPIO_KEY_TRACK_FORWARDED, /* Ctrl */
        TYPIO_KEY_TRACK_APP_SHORTCUT, /* T */
        TYPIO_KEY_TRACK_IDLE,
        TYPIO_KEY_TRACK_IDLE,
    };

    /* Modifier transitions alone must not rewrite forwarded key state. */
    ASSERT(states[0] == TYPIO_KEY_TRACK_FORWARDED);
    ASSERT(states[1] == TYPIO_KEY_TRACK_APP_SHORTCUT);

    /* Only lifecycle-boundary cleanup may convert forwarded keys. */
    ASSERT(typio_wl_key_tracking_mark_released_pending(states, 4) == 2);
    ASSERT(states[0] == TYPIO_KEY_TRACK_RELEASED_PENDING);
    ASSERT(states[1] == TYPIO_KEY_TRACK_RELEASED_PENDING);
}

TEST(app_shortcut_state_is_distinct_from_plain_forwarding) {
    TypioKeyTrackState states[4] = {
        TYPIO_KEY_TRACK_FORWARDED,
        TYPIO_KEY_TRACK_BASIC_PASSTHROUGH,
        TYPIO_KEY_TRACK_APP_SHORTCUT,
        TYPIO_KEY_TRACK_IDLE,
    };

    ASSERT(states[0] == TYPIO_KEY_TRACK_FORWARDED);
    ASSERT(states[1] == TYPIO_KEY_TRACK_BASIC_PASSTHROUGH);
    ASSERT(states[2] == TYPIO_KEY_TRACK_APP_SHORTCUT);
    ASSERT(states[3] == TYPIO_KEY_TRACK_IDLE);
}

int main(void) {
    printf("Running key tracking tests:\n");

    run_test_reset_clears_all_states();
    run_test_reset_clears_all_generations();
    run_test_mark_released_pending_only_changes_forwarded_keys();
    run_test_ctrl_shortcut_invariant_keeps_forwarded_key_until_boundary_cleanup();
    run_test_app_shortcut_state_is_distinct_from_plain_forwarding();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
