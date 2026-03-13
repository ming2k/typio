/**
 * @file test_lifecycle.c
 * @brief Lifecycle timing helper tests
 */

#include "lifecycle.h"

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

TEST(names_are_stable) {
    ASSERT(typio_wl_lifecycle_phase_name(TYPIO_WL_PHASE_INACTIVE)[0] == 'i');
    ASSERT(typio_wl_lifecycle_phase_name(TYPIO_WL_PHASE_ACTIVE)[0] == 'a');
}

TEST(valid_transitions_follow_timing_model) {
    ASSERT(typio_wl_lifecycle_transition_is_valid(
        TYPIO_WL_PHASE_INACTIVE, TYPIO_WL_PHASE_ACTIVATING));
    ASSERT(typio_wl_lifecycle_transition_is_valid(
        TYPIO_WL_PHASE_ACTIVATING, TYPIO_WL_PHASE_ACTIVE));
    ASSERT(typio_wl_lifecycle_transition_is_valid(
        TYPIO_WL_PHASE_ACTIVE, TYPIO_WL_PHASE_DEACTIVATING));
    ASSERT(typio_wl_lifecycle_transition_is_valid(
        TYPIO_WL_PHASE_DEACTIVATING, TYPIO_WL_PHASE_INACTIVE));
}

TEST(rejects_unexpected_shortcuts_between_phases) {
    ASSERT(!typio_wl_lifecycle_transition_is_valid(
        TYPIO_WL_PHASE_INACTIVE, TYPIO_WL_PHASE_ACTIVE));
    ASSERT(!typio_wl_lifecycle_transition_is_valid(
        TYPIO_WL_PHASE_ACTIVE, TYPIO_WL_PHASE_INACTIVE));
}

TEST(only_active_phase_allows_key_events) {
    ASSERT(!typio_wl_lifecycle_phase_allows_key_events(
        TYPIO_WL_PHASE_INACTIVE));
    ASSERT(!typio_wl_lifecycle_phase_allows_key_events(
        TYPIO_WL_PHASE_ACTIVATING));
    ASSERT(typio_wl_lifecycle_phase_allows_key_events(
        TYPIO_WL_PHASE_ACTIVE));
    ASSERT(!typio_wl_lifecycle_phase_allows_key_events(
        TYPIO_WL_PHASE_DEACTIVATING));
}

TEST(activating_and_active_phases_allow_modifier_events) {
    ASSERT(!typio_wl_lifecycle_phase_allows_modifier_events(
        TYPIO_WL_PHASE_INACTIVE));
    ASSERT(typio_wl_lifecycle_phase_allows_modifier_events(
        TYPIO_WL_PHASE_ACTIVATING));
    ASSERT(typio_wl_lifecycle_phase_allows_modifier_events(
        TYPIO_WL_PHASE_ACTIVE));
    ASSERT(!typio_wl_lifecycle_phase_allows_modifier_events(
        TYPIO_WL_PHASE_DEACTIVATING));
}

int main(void) {
    printf("Running lifecycle tests:\n");

    run_test_names_are_stable();
    run_test_valid_transitions_follow_timing_model();
    run_test_rejects_unexpected_shortcuts_between_phases();
    run_test_only_active_phase_allows_key_events();
    run_test_activating_and_active_phases_allow_modifier_events();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
