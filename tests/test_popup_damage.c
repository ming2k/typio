#include "popup_damage.h"

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

TEST(rejects_empty_input) {
    TypioPopupDamageRect rect = {};

    ASSERT(!typio_popup_damage_union(NULL, 0, &rect));
    ASSERT(!rect.valid);
}

TEST(returns_single_line_damage) {
    TypioPopupDamageLine lines[] = {
        {.x = 10, .y = 20, .width = 30, .height = 12},
    };
    TypioPopupDamageRect rect = {};

    ASSERT(typio_popup_damage_union(lines, 1, &rect));
    ASSERT(rect.valid);
    ASSERT(rect.x == 10);
    ASSERT(rect.y == 20);
    ASSERT(rect.width == 30);
    ASSERT(rect.height == 12);
}

TEST(merges_two_lines_into_bounding_box) {
    TypioPopupDamageLine lines[] = {
        {.x = 40, .y = 18, .width = 50, .height = 14},
        {.x = 12, .y = 40, .width = 30, .height = 10},
    };
    TypioPopupDamageRect rect = {};

    ASSERT(typio_popup_damage_union(lines, 2, &rect));
    ASSERT(rect.valid);
    ASSERT(rect.x == 12);
    ASSERT(rect.y == 18);
    ASSERT(rect.width == 78);
    ASSERT(rect.height == 32);
}

TEST(ignores_invalid_lines) {
    TypioPopupDamageLine lines[] = {
        {.x = 5, .y = 6, .width = 0, .height = 8},
        {.x = 8, .y = 9, .width = 7, .height = -1},
    };
    TypioPopupDamageRect rect = {};

    ASSERT(!typio_popup_damage_union(lines, 2, &rect));
    ASSERT(!rect.valid);
}

int main(void) {
    printf("Running popup damage tests:\n");

    run_test_rejects_empty_input();
    run_test_returns_single_line_damage();
    run_test_merges_two_lines_into_bounding_box();
    run_test_ignores_invalid_lines();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
