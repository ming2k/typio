#include "text_ui_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

TEST(syncs_popup_only_when_preedit_and_cursor_match) {
    ASSERT_EQ(typio_wl_text_ui_plan_update("ni", 2, "ni", 2),
              TYPIO_WL_TEXT_UI_SYNC_POPUP_ONLY);
}

TEST(syncs_when_preedit_text_changes) {
    ASSERT_EQ(typio_wl_text_ui_plan_update("ni", 2, "nih", 3),
              TYPIO_WL_TEXT_UI_SYNC_PREEDIT_AND_POPUP);
}

TEST(syncs_when_cursor_changes) {
    ASSERT_EQ(typio_wl_text_ui_plan_update("ni", 1, "ni", 2),
              TYPIO_WL_TEXT_UI_SYNC_PREEDIT_AND_POPUP);
}

TEST(treats_null_preedit_as_empty_string) {
    ASSERT_EQ(typio_wl_text_ui_plan_update(nullptr, -1, nullptr, -1),
              TYPIO_WL_TEXT_UI_SYNC_POPUP_ONLY);
    ASSERT_EQ(typio_wl_text_ui_plan_update(nullptr, -1, "", -1),
              TYPIO_WL_TEXT_UI_SYNC_POPUP_ONLY);
    ASSERT_EQ(typio_wl_text_ui_plan_update("", -1, "ni", 2),
              TYPIO_WL_TEXT_UI_SYNC_PREEDIT_AND_POPUP);
}

TEST(reset_tracking_clears_pending_and_preedit_state) {
    bool popup_update_pending = true;
    char *last_preedit_text = strdup("ni");
    int last_preedit_cursor = 2;

    ASSERT(last_preedit_text != nullptr);

    typio_wl_text_ui_reset_tracking(&popup_update_pending,
                                    &last_preedit_text,
                                    &last_preedit_cursor);

    ASSERT_EQ(popup_update_pending, false);
    ASSERT_EQ(last_preedit_text, nullptr);
    ASSERT_EQ(last_preedit_cursor, -1);
}

TEST(reset_tracking_accepts_null_fields) {
    bool popup_update_pending = true;

    typio_wl_text_ui_reset_tracking(&popup_update_pending, nullptr, nullptr);
    ASSERT_EQ(popup_update_pending, false);

    typio_wl_text_ui_reset_tracking(nullptr, nullptr, nullptr);
}

TEST(flush_popup_update_requires_focused_context) {
    ASSERT_EQ(typio_wl_text_ui_should_flush_popup_update(true, true, true, true), true);
    ASSERT_EQ(typio_wl_text_ui_should_flush_popup_update(false, true, true, true), false);
    ASSERT_EQ(typio_wl_text_ui_should_flush_popup_update(true, false, true, true), false);
    ASSERT_EQ(typio_wl_text_ui_should_flush_popup_update(true, true, false, true), false);
    ASSERT_EQ(typio_wl_text_ui_should_flush_popup_update(true, true, true, false), false);
}

int main(void) {
    printf("Running text UI state tests:\n");
    run_test_syncs_popup_only_when_preedit_and_cursor_match();
    run_test_syncs_when_preedit_text_changes();
    run_test_syncs_when_cursor_changes();
    run_test_treats_null_preedit_as_empty_string();
    run_test_reset_tracking_clears_pending_and_preedit_state();
    run_test_reset_tracking_accepts_null_fields();
    run_test_flush_popup_update_requires_focused_context();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
