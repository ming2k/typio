
#include "path_expand.h"

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

TEST(expands_home_prefix) {
    char *expanded;

    ASSERT(setenv("HOME", "/tmp/rime-home", 1) == 0);
    expanded = typio_rime_expand_path("~/data");
    ASSERT(expanded != nullptr);
    ASSERT_STR_EQ(expanded, "/tmp/rime-home/data");
    free(expanded);
}

TEST(expands_environment_variables) {
    char *expanded;

    ASSERT(setenv("XDG_DATA_HOME", "/tmp/xdg-data", 1) == 0);
    expanded = typio_rime_expand_path("${XDG_DATA_HOME}/typio/rime");
    ASSERT(expanded != nullptr);
    ASSERT_STR_EQ(expanded, "/tmp/xdg-data/typio/rime");
    free(expanded);
}

TEST(expands_plain_dollar_variables) {
    char *expanded;

    ASSERT(setenv("HOME", "/tmp/plain-home", 1) == 0);
    expanded = typio_rime_expand_path("$HOME/rime");
    ASSERT(expanded != nullptr);
    ASSERT_STR_EQ(expanded, "/tmp/plain-home/rime");
    free(expanded);
}

TEST(preserves_unknown_variables) {
    char *expanded;

    ASSERT(unsetenv("TYPO_UNKNOWN_PATH") == 0);
    expanded = typio_rime_expand_path("$TYPO_UNKNOWN_PATH/rime");
    ASSERT(expanded != nullptr);
    ASSERT_STR_EQ(expanded, "$TYPO_UNKNOWN_PATH/rime");
    free(expanded);
}

int main(void) {
    printf("Running Rime path expansion tests:\n");
    run_test_expands_home_prefix();
    run_test_expands_environment_variables();
    run_test_expands_plain_dollar_variables();
    run_test_preserves_unknown_variables();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
