/**
 * @file test_modifier_policy.c
 * @brief Modifier truth-source policy tests
 */

#include "modifier_policy.h"

#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon-keysyms.h>

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

TEST(uses_physical_modifiers_for_owned_generation) {
    uint32_t mods = typio_wl_modifier_policy_effective_modifiers(
        TYPIO_MOD_CTRL,
        TYPIO_MOD_NONE,
        true,
        XKB_KEY_w,
        WL_KEYBOARD_KEY_STATE_PRESSED);

    ASSERT((mods & TYPIO_MOD_CTRL) != 0);
}

TEST(falls_back_to_xkb_blocking_modifiers_before_generation_is_owned) {
    uint32_t mods = typio_wl_modifier_policy_effective_modifiers(
        TYPIO_MOD_NONE,
        TYPIO_MOD_CTRL,
        false,
        XKB_KEY_w,
        WL_KEYBOARD_KEY_STATE_PRESSED);

    ASSERT((mods & TYPIO_MOD_CTRL) != 0);
}

TEST(unowned_generation_keeps_blocking_modifier_visible_for_new_shortcuts) {
    uint32_t mods = typio_wl_modifier_policy_effective_modifiers(
        TYPIO_MOD_NONE,
        TYPIO_MOD_CTRL,
        false,
        XKB_KEY_w,
        WL_KEYBOARD_KEY_STATE_PRESSED);

    ASSERT((mods & TYPIO_MOD_CTRL) != 0);
}

TEST(non_blocking_xkb_modifiers_do_not_override_owned_generation) {
    uint32_t mods = typio_wl_modifier_policy_effective_modifiers(
        TYPIO_MOD_NONE,
        TYPIO_MOD_SHIFT,
        true,
        XKB_KEY_w,
        WL_KEYBOARD_KEY_STATE_PRESSED);

    ASSERT((mods & TYPIO_MOD_SHIFT) == 0);
}

TEST(current_modifier_key_is_folded_into_pressed_event) {
    uint32_t mods = typio_wl_modifier_policy_effective_modifiers(
        TYPIO_MOD_NONE,
        TYPIO_MOD_NONE,
        true,
        XKB_KEY_Control_L,
        WL_KEYBOARD_KEY_STATE_PRESSED);

    ASSERT((mods & TYPIO_MOD_CTRL) != 0);
}

TEST(sync_physical_modifiers_adopts_current_xkb_blocking_modifiers) {
    uint32_t mods = typio_wl_modifier_policy_sync_physical_modifiers(
        TYPIO_MOD_NONE,
        TYPIO_MOD_CTRL | TYPIO_MOD_ALT);

    ASSERT((mods & TYPIO_MOD_CTRL) != 0);
    ASSERT((mods & TYPIO_MOD_ALT) != 0);
}

TEST(sync_physical_modifiers_clears_blocking_modifiers_missing_from_xkb) {
    uint32_t mods = typio_wl_modifier_policy_sync_physical_modifiers(
        TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT,
        TYPIO_MOD_SHIFT);

    ASSERT((mods & TYPIO_MOD_CTRL) == 0);
    ASSERT((mods & TYPIO_MOD_SHIFT) != 0);
}

int main(void) {
    printf("Running modifier policy tests:\n");

    run_test_uses_physical_modifiers_for_owned_generation();
    run_test_falls_back_to_xkb_blocking_modifiers_before_generation_is_owned();
    run_test_unowned_generation_keeps_blocking_modifier_visible_for_new_shortcuts();
    run_test_non_blocking_xkb_modifiers_do_not_override_owned_generation();
    run_test_current_modifier_key_is_folded_into_pressed_event();
    run_test_sync_physical_modifiers_adopts_current_xkb_blocking_modifiers();
    run_test_sync_physical_modifiers_clears_blocking_modifiers_missing_from_xkb();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
