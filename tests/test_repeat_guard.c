/**
 * @file test_repeat_guard.c
 * @brief Repeat cancellation helper tests
 */

#include "repeat_guard.h"
#include "typio/types.h"

#include <assert.h>
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

TEST(cancels_when_ctrl_changes) {
    ASSERT(typio_wl_repeat_should_cancel_on_modifier_transition(
        TYPIO_MOD_CTRL,
        TYPIO_MOD_NONE));
    ASSERT(typio_wl_repeat_should_cancel_on_modifier_transition(
        TYPIO_MOD_NONE,
        TYPIO_MOD_CTRL));
}

TEST(cancels_when_alt_or_super_changes) {
    ASSERT(typio_wl_repeat_should_cancel_on_modifier_transition(
        TYPIO_MOD_ALT,
        TYPIO_MOD_NONE));
    ASSERT(typio_wl_repeat_should_cancel_on_modifier_transition(
        TYPIO_MOD_SUPER,
        TYPIO_MOD_ALT));
}

TEST(ignores_shift_only_transitions) {
    ASSERT(!typio_wl_repeat_should_cancel_on_modifier_transition(
        TYPIO_MOD_SHIFT,
        TYPIO_MOD_NONE));
    ASSERT(!typio_wl_repeat_should_cancel_on_modifier_transition(
        TYPIO_MOD_SHIFT,
        TYPIO_MOD_SHIFT | TYPIO_MOD_CAPSLOCK));
}

TEST(blocks_repeat_for_suppressed_or_pending_states) {
    ASSERT(!typio_wl_repeat_should_run_for_state(
        TYPIO_KEY_TRACK_SUPPRESSED_STARTUP));
    ASSERT(!typio_wl_repeat_should_run_for_state(
        TYPIO_KEY_TRACK_RELEASED_PENDING));
}

TEST(allows_repeat_for_normal_owned_routes) {
    ASSERT(typio_wl_repeat_should_run_for_state(TYPIO_KEY_TRACK_IDLE));
    ASSERT(typio_wl_repeat_should_run_for_state(TYPIO_KEY_TRACK_FORWARDED));
    ASSERT(typio_wl_repeat_should_run_for_state(TYPIO_KEY_TRACK_APP_SHORTCUT));
}

int main(void) {
    printf("Running repeat guard tests:\n");

    run_test_cancels_when_ctrl_changes();
    run_test_cancels_when_alt_or_super_changes();
    run_test_ignores_shift_only_transitions();
    run_test_blocks_repeat_for_suppressed_or_pending_states();
    run_test_allows_repeat_for_normal_owned_routes();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
