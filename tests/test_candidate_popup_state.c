#include "candidate_popup_state.h"

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

#define ASSERT_EQ(a, b) ASSERT((a) == (b))

TEST(invalidate_config_clears_all_reusable_popup_caches) {
    TypioCandidatePopupInvalidationState state = {
        .config_cache_valid = true,
        .theme_cache_valid = true,
        .render_cache_valid = true,
    };

    typio_candidate_popup_state_invalidate_config(&state);

    ASSERT_EQ(state.config_cache_valid, false);
    ASSERT_EQ(state.theme_cache_valid, false);
    ASSERT_EQ(state.render_cache_valid, false);
}

TEST(output_change_ignores_unrelated_popup) {
    ASSERT_EQ(typio_candidate_popup_state_handle_output_change(false, true, true),
              TYPIO_CANDIDATE_POPUP_OUTPUT_CHANGE_IGNORE);
    ASSERT_EQ(typio_candidate_popup_state_handle_output_change(true, false, true),
              TYPIO_CANDIDATE_POPUP_OUTPUT_CHANGE_IGNORE);
}

TEST(output_change_untracks_removed_output) {
    ASSERT_EQ(typio_candidate_popup_state_handle_output_change(true, true, false),
              TYPIO_CANDIDATE_POPUP_OUTPUT_CHANGE_UNTRACK);
}

TEST(output_change_refreshes_tracked_known_output) {
    ASSERT_EQ(typio_candidate_popup_state_handle_output_change(true, true, true),
              TYPIO_CANDIDATE_POPUP_OUTPUT_CHANGE_REFRESH);
}

int main(void) {
    printf("Running popup state tests:\n");
    run_test_invalidate_config_clears_all_reusable_popup_caches();
    run_test_output_change_ignores_unrelated_popup();
    run_test_output_change_untracks_removed_output();
    run_test_output_change_refreshes_tracked_known_output();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
