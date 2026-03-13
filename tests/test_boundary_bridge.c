/**
 * @file test_boundary_bridge.c
 * @brief Boundary bridge policy tests
 */

#include "boundary_bridge.h"
#include "typio/event.h"
#include "typio/types.h"

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

TEST(cleans_up_orphan_release_for_shortcut_modifiers) {
    ASSERT(typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_space,
        TYPIO_MOD_CTRL, false));
    ASSERT(typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_space,
        TYPIO_MOD_ALT, false));
    ASSERT(typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_space,
        TYPIO_MOD_SUPER, false));
    ASSERT(!typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_space,
        TYPIO_MOD_SHIFT, false));
    ASSERT(!typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_space,
        TYPIO_MOD_NONE, false));
}

TEST(cleans_up_orphan_release_after_modifier_was_seen) {
    ASSERT(typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_space,
        TYPIO_MOD_NONE, true));
}

TEST(cleans_up_orphan_release_for_enter_without_modifiers) {
    ASSERT(typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_Return,
        TYPIO_MOD_NONE, false));
    ASSERT(typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
        TYPIO_KEY_KP_Enter,
        TYPIO_MOD_NONE, false));
}

TEST(resets_carried_modifiers_outside_deactivating) {
    ASSERT(typio_wl_boundary_bridge_should_reset_carried_modifiers(
        TYPIO_WL_PHASE_ACTIVE, true));
    ASSERT(typio_wl_boundary_bridge_should_reset_carried_modifiers(
        TYPIO_WL_PHASE_ACTIVATING, true));
    ASSERT(!typio_wl_boundary_bridge_should_reset_carried_modifiers(
        TYPIO_WL_PHASE_DEACTIVATING, true));
    ASSERT(!typio_wl_boundary_bridge_should_reset_carried_modifiers(
        TYPIO_WL_PHASE_INACTIVE, false));
}

TEST(carries_modifiers_only_for_owned_deactivation_with_mask) {
    ASSERT(typio_wl_boundary_bridge_should_carry_modifiers(
        TYPIO_WL_PHASE_DEACTIVATING, true, 1, 0, 0));
    ASSERT(typio_wl_boundary_bridge_should_carry_modifiers(
        TYPIO_WL_PHASE_DEACTIVATING, true, 0, 1, 0));
    ASSERT(typio_wl_boundary_bridge_should_carry_modifiers(
        TYPIO_WL_PHASE_DEACTIVATING, true, 0, 0, 1));
    ASSERT(!typio_wl_boundary_bridge_should_carry_modifiers(
        TYPIO_WL_PHASE_ACTIVE, true, 1, 0, 0));
    ASSERT(!typio_wl_boundary_bridge_should_carry_modifiers(
        TYPIO_WL_PHASE_DEACTIVATING, false, 1, 0, 0));
    ASSERT(!typio_wl_boundary_bridge_should_carry_modifiers(
        TYPIO_WL_PHASE_DEACTIVATING, true, 0, 0, 0));
}

int main(void) {
    printf("Running boundary bridge tests:\n");

    run_test_cleans_up_orphan_release_for_shortcut_modifiers();
    run_test_cleans_up_orphan_release_after_modifier_was_seen();
    run_test_cleans_up_orphan_release_for_enter_without_modifiers();
    run_test_resets_carried_modifiers_outside_deactivating();
    run_test_carries_modifiers_only_for_owned_deactivation_with_mask();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
