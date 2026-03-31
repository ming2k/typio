#include "candidate_popup_render_state.h"

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

static TypioCandidatePopupRenderState make_state(void) {
    TypioCandidatePopupRenderState state = {
        .cache_valid = true,
        .line_count = 2,
        .content_signature = 42,
        .palette_token = (const void *)0x1,
        .theme_mode = 1,
        .layout_mode = 0,
        .font_size = 11,
        .font_desc = "Sans 11",
        .page_font_desc = "Sans 10",
        .width = 200,
        .height = 60,
        .preedit_text = "ni",
    };
    return state;
}

TEST(matches_when_layout_inputs_are_identical) {
    TypioCandidatePopupRenderState cached = make_state();
    TypioCandidatePopupRenderState current = make_state();

    ASSERT_EQ(typio_candidate_popup_render_state_matches(&cached, &current, 1), true);
}

TEST(rejects_when_cache_was_invalidated) {
    TypioCandidatePopupRenderState cached = make_state();
    TypioCandidatePopupRenderState current = make_state();

    cached.cache_valid = false;
    ASSERT_EQ(typio_candidate_popup_render_state_matches(&cached, &current, 1), false);
}

TEST(rejects_when_config_reload_changes_layout_inputs) {
    TypioCandidatePopupRenderState cached = make_state();
    TypioCandidatePopupRenderState current = make_state();

    current.font_size = 14;
    current.font_desc = "Sans 14";
    current.page_font_desc = "Sans 13";
    ASSERT_EQ(typio_candidate_popup_render_state_matches(&cached, &current, 1), false);
}

TEST(rejects_when_scale_or_preedit_changes) {
    TypioCandidatePopupRenderState cached = make_state();
    TypioCandidatePopupRenderState current = make_state();

    current.preedit_text = "nih";
    ASSERT_EQ(typio_candidate_popup_render_state_matches(&cached, &current, 1), false);

    current = make_state();
    cached.width = -1;
    ASSERT_EQ(typio_candidate_popup_render_state_matches(&cached, &current, 2), false);
}

TEST(rejects_when_content_signature_or_palette_changes) {
    TypioCandidatePopupRenderState cached = make_state();
    TypioCandidatePopupRenderState current = make_state();

    current.content_signature = 99;
    ASSERT_EQ(typio_candidate_popup_render_state_matches(&cached, &current, 1), false);

    current = make_state();
    current.palette_token = (const void *)0x2;
    ASSERT_EQ(typio_candidate_popup_render_state_matches(&cached, &current, 1), false);
}

TEST(static_match_ignores_preedit_and_mode_changes) {
    TypioCandidatePopupRenderState cached = make_state();
    TypioCandidatePopupRenderState current = make_state();

    cached.mode_label = "Rime CN";
    current.preedit_text = "nih";
    current.mode_label = "Rime EN";

    ASSERT_EQ(typio_candidate_popup_render_state_matches_static(&cached, &current, 1), true);
    ASSERT_EQ(typio_candidate_popup_render_state_matches(&cached, &current, 1), false);
}

int main(void) {
    printf("Running popup render state tests:\n");
    run_test_matches_when_layout_inputs_are_identical();
    run_test_rejects_when_cache_was_invalidated();
    run_test_rejects_when_config_reload_changes_layout_inputs();
    run_test_rejects_when_scale_or_preedit_changes();
    run_test_rejects_when_content_signature_or_palette_changes();
    run_test_static_match_ignores_preedit_and_mode_changes();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
