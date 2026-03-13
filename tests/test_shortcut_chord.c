/**
 * @file test_shortcut_chord.c
 * @brief Tests for modifier-only shortcut chord policy
 */

#include "shortcut_chord.h"
#include "typio/event.h"

#include <stdbool.h>
#include <stdint.h>
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

TEST(triggers_only_for_pure_ctrl_shift_chord) {
    ASSERT(typio_wl_shortcut_chord_should_switch_engine(
        TYPIO_KEY_Shift_L,
        TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT,
        false,
        false));
    ASSERT(typio_wl_shortcut_chord_should_switch_engine(
        TYPIO_KEY_Control_L,
        TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT,
        false,
        false));
    ASSERT(!typio_wl_shortcut_chord_should_switch_engine(
        TYPIO_KEY_Alt_L,
        TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT,
        false,
        false));
    ASSERT(!typio_wl_shortcut_chord_should_switch_engine(
        TYPIO_KEY_Shift_L,
        TYPIO_MOD_SHIFT,
        false,
        false));
    ASSERT(!typio_wl_shortcut_chord_should_switch_engine(
        TYPIO_KEY_Shift_L,
        TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT | TYPIO_MOD_ALT,
        false,
        false));
}

TEST(does_not_trigger_after_non_modifier_or_repeat_fire) {
    ASSERT(!typio_wl_shortcut_chord_should_switch_engine(
        TYPIO_KEY_Shift_L,
        TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT,
        true,
        false));
    ASSERT(!typio_wl_shortcut_chord_should_switch_engine(
        TYPIO_KEY_Shift_L,
        TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT,
        false,
        true));
}

TEST(resets_only_after_all_shortcut_modifiers_are_up) {
    ASSERT(!typio_wl_shortcut_chord_should_reset(TYPIO_MOD_CTRL));
    ASSERT(!typio_wl_shortcut_chord_should_reset(TYPIO_MOD_SHIFT));
    ASSERT(!typio_wl_shortcut_chord_should_reset(TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT));
    ASSERT(!typio_wl_shortcut_chord_should_reset(TYPIO_MOD_ALT));
    ASSERT(typio_wl_shortcut_chord_should_reset(TYPIO_MOD_NONE));
}

int main(void) {
    printf("Running shortcut chord tests:\n");
    run_test_triggers_only_for_pure_ctrl_shift_chord();
    run_test_does_not_trigger_after_non_modifier_or_repeat_fire();
    run_test_resets_only_after_all_shortcut_modifiers_are_up();
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
