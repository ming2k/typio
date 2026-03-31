#include "candidate_popup_layout.h"

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
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_NOT_NULL(a) ASSERT((a) != NULL)

static TypioCandidateList make_candidates(void) {
    static TypioCandidate entries[] = {
        {.text = "中国", .comment = "zhong guo", .label = "1"},
        {.text = "中心", .comment = "zhong xin", .label = "2"},
    };

    TypioCandidateList candidates = {
        .candidates = entries,
        .count = sizeof(entries) / sizeof(entries[0]),
        .selected = 0,
        .content_signature = 1234,
    };
    return candidates;
}

static TypioCandidatePopupRenderConfig make_config(TypioCandidatePopupLayoutMode layout_mode) {
    TypioCandidatePopupRenderConfig config = {
        .theme_mode = 0,
        .layout_mode = layout_mode,
        .font_size = 13,
        .mode_indicator = true,
    };

    snprintf(config.font_desc, sizeof(config.font_desc), "%s", "Sans 13");
    snprintf(config.page_font_desc, sizeof(config.page_font_desc), "%s", "Sans 11");
    return config;
}

static void free_transient_layout(TypioCandidatePopupLine *lines,
                                  size_t line_count,
                                  char *preedit_text,
                                  PangoLayout *preedit_layout,
                                  char *mode_label,
                                  PangoLayout *mode_label_layout) {
    TypioCandidatePopupCache cache = {
        .lines = lines,
        .line_count = line_count,
        .preedit_text = preedit_text,
        .preedit_layout = preedit_layout,
        .mode_label = mode_label,
        .mode_label_layout = mode_label_layout,
    };

    typio_candidate_popup_layout_cache_invalidate(&cache);
}

TEST(compute_creates_cached_layouts_and_positions_for_vertical_mode) {
    TypioCandidateList candidates = make_candidates();
    TypioCandidatePopupRenderConfig config = make_config(TYPIO_CANDIDATE_POPUP_LAYOUT_VERTICAL);
    TypioCandidatePopupFontCache font_cache = {};
    TypioCandidatePopupLine *lines = NULL;
    size_t line_count = 0;
    PangoLayout *preedit_layout = NULL;
    PangoLayout *mode_label_layout = NULL;
    char *preedit_text = strdup("zhong");
    char *mode_label = strdup("Rime CN");
    int preedit_width = 0;
    int preedit_height = 0;
    int preedit_x = 0;
    int preedit_y = 0;
    int mode_label_width = 0;
    int mode_label_height = 0;
    int mode_label_x = 0;
    int mode_label_y = 0;
    int mode_label_divider_y = -1;
    int width = 0;
    int height = 0;

    ASSERT_NOT_NULL(preedit_text);
    ASSERT_NOT_NULL(mode_label);
    ASSERT(typio_candidate_popup_layout_compute(&candidates, preedit_text, mode_label,
                                                &config, &font_cache,
                                                &lines, &line_count,
                                                &preedit_layout,
                                                &preedit_width, &preedit_height,
                                                &preedit_x, &preedit_y,
                                                &mode_label_layout,
                                                &mode_label_width, &mode_label_height,
                                                &mode_label_x, &mode_label_y,
                                                &mode_label_divider_y,
                                                &width, &height));

    ASSERT_EQ(line_count, candidates.count);
    ASSERT_NOT_NULL(preedit_layout);
    ASSERT_NOT_NULL(mode_label_layout);
    ASSERT(width >= 220);
    ASSERT(preedit_width > 0);
    ASSERT(preedit_height > 0);
    ASSERT(mode_label_width > 0);
    ASSERT(mode_label_height > 0);
    ASSERT_EQ(preedit_x, 8);
    ASSERT_EQ(preedit_y, 8);
    ASSERT(mode_label_divider_y > 0);
    ASSERT(mode_label_y > mode_label_divider_y);
    ASSERT(mode_label_x + mode_label_width <= width - 8);

    for (size_t i = 0; i < line_count; ++i) {
        ASSERT_NOT_NULL(lines[i].text);
        ASSERT_NOT_NULL(lines[i].layout);
        ASSERT_EQ(lines[i].x, 8);
        ASSERT_EQ(lines[i].text_x, lines[i].x + 4);
        ASSERT_EQ(lines[i].text_y, lines[i].y + 2);
        ASSERT(lines[i].width == width - 16);
        ASSERT(lines[i].height >= lines[i].text_height + 4);
    }
    ASSERT(lines[1].y > lines[0].y);

    free_transient_layout(lines, line_count, preedit_text, preedit_layout,
                          mode_label, mode_label_layout);
    typio_candidate_popup_font_cache_free(&font_cache);
}

TEST(compute_places_mode_label_on_candidate_row_for_horizontal_mode) {
    TypioCandidateList candidates = make_candidates();
    TypioCandidatePopupRenderConfig config = make_config(TYPIO_CANDIDATE_POPUP_LAYOUT_HORIZONTAL);
    TypioCandidatePopupFontCache font_cache = {};
    TypioCandidatePopupLine *lines = NULL;
    size_t line_count = 0;
    PangoLayout *preedit_layout = NULL;
    PangoLayout *mode_label_layout = NULL;
    char *preedit_text = strdup("ni");
    char *mode_label = strdup("Mozc A");
    int preedit_width = 0;
    int preedit_height = 0;
    int preedit_x = 0;
    int preedit_y = 0;
    int mode_label_width = 0;
    int mode_label_height = 0;
    int mode_label_x = 0;
    int mode_label_y = 0;
    int mode_label_divider_y = -1;
    int width = 0;
    int height = 0;

    ASSERT_NOT_NULL(preedit_text);
    ASSERT_NOT_NULL(mode_label);
    ASSERT(typio_candidate_popup_layout_compute(&candidates, preedit_text, mode_label,
                                                &config, &font_cache,
                                                &lines, &line_count,
                                                &preedit_layout,
                                                &preedit_width, &preedit_height,
                                                &preedit_x, &preedit_y,
                                                &mode_label_layout,
                                                &mode_label_width, &mode_label_height,
                                                &mode_label_x, &mode_label_y,
                                                &mode_label_divider_y,
                                                &width, &height));

    ASSERT_EQ(line_count, candidates.count);
    ASSERT_NOT_NULL(preedit_layout);
    ASSERT_NOT_NULL(mode_label_layout);
    ASSERT_EQ(mode_label_divider_y, -1);
    ASSERT_EQ(lines[0].y, lines[1].y);
    ASSERT(lines[1].x > lines[0].x);
    ASSERT_EQ(mode_label_y, lines[0].y + 2);
    ASSERT(mode_label_x + mode_label_width <= width - 8);

    free_transient_layout(lines, line_count, preedit_text, preedit_layout,
                          mode_label, mode_label_layout);
    typio_candidate_popup_font_cache_free(&font_cache);
}

TEST(cache_invalidate_clears_owned_layout_state) {
    TypioCandidateList candidates = make_candidates();
    TypioCandidatePopupRenderConfig config = make_config(TYPIO_CANDIDATE_POPUP_LAYOUT_VERTICAL);
    TypioCandidatePopupFontCache font_cache = {};
    TypioCandidatePopupCache cache = {};
    TypioCandidatePopupLine *lines = NULL;
    size_t line_count = 0;
    PangoLayout *preedit_layout = NULL;
    PangoLayout *mode_label_layout = NULL;
    char *preedit_text = strdup("abc");
    char *mode_label = strdup("Mode");
    int preedit_width = 0;
    int preedit_height = 0;
    int preedit_x = 0;
    int preedit_y = 0;
    int mode_label_width = 0;
    int mode_label_height = 0;
    int mode_label_x = 0;
    int mode_label_y = 0;
    int mode_label_divider_y = -1;
    int width = 0;
    int height = 0;

    ASSERT_NOT_NULL(preedit_text);
    ASSERT_NOT_NULL(mode_label);
    ASSERT(typio_candidate_popup_layout_compute(&candidates, preedit_text, mode_label,
                                                &config, &font_cache,
                                                &lines, &line_count,
                                                &preedit_layout,
                                                &preedit_width, &preedit_height,
                                                &preedit_x, &preedit_y,
                                                &mode_label_layout,
                                                &mode_label_width, &mode_label_height,
                                                &mode_label_x, &mode_label_y,
                                                &mode_label_divider_y,
                                                &width, &height));

    typio_candidate_popup_layout_cache_store(&cache,
                                             lines, line_count,
                                             candidates.selected,
                                             candidates.content_signature,
                                             preedit_text,
                                             preedit_layout,
                                             preedit_width, preedit_height,
                                             preedit_x, preedit_y,
                                             mode_label,
                                             mode_label_layout,
                                             mode_label_width, mode_label_height,
                                             mode_label_x, mode_label_y,
                                             mode_label_divider_y,
                                             width, height,
                                             &config, NULL);
    ASSERT(cache.valid);
    ASSERT_NOT_NULL(cache.lines);
    ASSERT_NOT_NULL(cache.preedit_layout);
    ASSERT_NOT_NULL(cache.mode_label_layout);

    typio_candidate_popup_layout_cache_invalidate(&cache);
    ASSERT(!cache.valid);
    ASSERT(cache.lines == NULL);
    ASSERT(cache.preedit_text == NULL);
    ASSERT(cache.preedit_layout == NULL);
    ASSERT(cache.mode_label == NULL);
    ASSERT(cache.mode_label_layout == NULL);
    ASSERT_EQ(cache.line_count, 0);
    ASSERT_EQ(cache.selected, -1);

    typio_candidate_popup_font_cache_free(&font_cache);
}

TEST(measure_aux_reuses_line_geometry_when_popup_size_stays_fixed) {
    TypioCandidateList candidates = make_candidates();
    TypioCandidatePopupRenderConfig config = make_config(TYPIO_CANDIDATE_POPUP_LAYOUT_VERTICAL);
    TypioCandidatePopupFontCache font_cache = {};
    TypioCandidatePopupCache cache = {};
    TypioCandidatePopupLine *lines = NULL;
    size_t line_count = 0;
    PangoLayout *preedit_layout = NULL;
    PangoLayout *mode_label_layout = NULL;
    char *preedit_text = strdup("ni");
    char *mode_label = strdup("Mode A");
    int preedit_width = 0;
    int preedit_height = 0;
    int preedit_x = 0;
    int preedit_y = 0;
    int mode_label_width = 0;
    int mode_label_height = 0;
    int mode_label_x = 0;
    int mode_label_y = 0;
    int mode_label_divider_y = -1;
    int width = 0;
    int height = 0;
    PangoLayout *next_preedit_layout = NULL;
    PangoLayout *next_mode_label_layout = NULL;
    int next_preedit_width = 0;
    int next_preedit_height = 0;
    int next_preedit_x = 0;
    int next_preedit_y = 0;
    int next_mode_label_width = 0;
    int next_mode_label_height = 0;
    int next_mode_label_x = 0;
    int next_mode_label_y = 0;
    int next_mode_label_divider_y = -1;
    int next_width = 0;
    int next_height = 0;
    char *next_preedit_text = strdup("na");
    char *next_mode_label = strdup("Mode B");

    ASSERT_NOT_NULL(preedit_text);
    ASSERT_NOT_NULL(mode_label);
    ASSERT_NOT_NULL(next_preedit_text);
    ASSERT_NOT_NULL(next_mode_label);
    ASSERT(typio_candidate_popup_layout_compute(&candidates, preedit_text, mode_label,
                                                &config, &font_cache,
                                                &lines, &line_count,
                                                &preedit_layout,
                                                &preedit_width, &preedit_height,
                                                &preedit_x, &preedit_y,
                                                &mode_label_layout,
                                                &mode_label_width, &mode_label_height,
                                                &mode_label_x, &mode_label_y,
                                                &mode_label_divider_y,
                                                &width, &height));

    typio_candidate_popup_layout_cache_store(&cache,
                                             lines, line_count,
                                             candidates.selected,
                                             candidates.content_signature,
                                             preedit_text,
                                             preedit_layout,
                                             preedit_width, preedit_height,
                                             preedit_x, preedit_y,
                                             mode_label,
                                             mode_label_layout,
                                             mode_label_width, mode_label_height,
                                             mode_label_x, mode_label_y,
                                             mode_label_divider_y,
                                             width, height,
                                             &config, NULL);

    ASSERT(typio_candidate_popup_layout_measure_aux(&cache,
                                                    next_preedit_text,
                                                    next_mode_label,
                                                    &config,
                                                    &font_cache,
                                                    &next_preedit_layout,
                                                    &next_preedit_width,
                                                    &next_preedit_height,
                                                    &next_preedit_x,
                                                    &next_preedit_y,
                                                    &next_mode_label_layout,
                                                    &next_mode_label_width,
                                                    &next_mode_label_height,
                                                    &next_mode_label_x,
                                                    &next_mode_label_y,
                                                    &next_mode_label_divider_y,
                                                    &next_width,
                                                    &next_height));

    ASSERT_NOT_NULL(next_preedit_layout);
    ASSERT_NOT_NULL(next_mode_label_layout);
    ASSERT_EQ(next_width, cache.width);
    ASSERT_EQ(next_height, cache.height);
    ASSERT_EQ(next_preedit_x, cache.preedit_x);
    ASSERT_EQ(next_preedit_y, cache.preedit_y);
    ASSERT_EQ(next_preedit_height, cache.preedit_height);
    ASSERT_EQ(next_mode_label_height, cache.mode_label_height);
    ASSERT_EQ(next_mode_label_divider_y, cache.mode_label_divider_y);

    typio_candidate_popup_layout_cache_invalidate(&cache);
    free(next_preedit_text);
    free(next_mode_label);
    g_object_unref(next_preedit_layout);
    g_object_unref(next_mode_label_layout);
    typio_candidate_popup_font_cache_free(&font_cache);
}

int main(void) {
    printf("Running popup layout tests:\n");
    run_test_compute_creates_cached_layouts_and_positions_for_vertical_mode();
    run_test_compute_places_mode_label_on_candidate_row_for_horizontal_mode();
    run_test_cache_invalidate_clears_owned_layout_state();
    run_test_measure_aux_reuses_line_geometry_when_popup_size_stays_fixed();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
