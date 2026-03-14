#include "preedit_format.h"

#include <assert.h>
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

#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)
#define ASSERT_EQ(a, b) ASSERT((a) == (b))

TEST(plain_preedit_only) {
    TypioPreeditSegment segments[] = {
        {.text = "zhong", .format = 0},
        {.text = "wen", .format = 0},
    };
    TypioPreedit preedit = {
        .segments = segments,
        .segment_count = 2,
        .cursor_pos = 5,
    };
    int cursor_pos = -1;
    char *display = typio_wl_build_plain_preedit(&preedit, &cursor_pos);

    ASSERT(display != NULL);
    ASSERT_STR_EQ(display, "zhongwen");
    ASSERT_EQ(cursor_pos, 5);

    free(display);
}

TEST(plain_preedit_null) {
    int cursor_pos = 99;
    char *display = typio_wl_build_plain_preedit(NULL, &cursor_pos);

    ASSERT(display == NULL);
    ASSERT_EQ(cursor_pos, -1);
}

int main(void) {
    printf("Running inline UI tests:\n");
    run_test_plain_preedit_only();
    run_test_plain_preedit_null();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
