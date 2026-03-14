#include "icon_pixmap.h"

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

static int alpha_sum(const unsigned char *data, int data_len) {
    int sum = 0;

    for (int i = 0; i < data_len; i += 4) {
        sum += data[i];
    }

    return sum;
}

TEST(build_basic_icon) {
    unsigned char *data = nullptr;
    int width = 0;
    int height = 0;
    int data_len = 0;

    ASSERT(typio_tray_icon_pixmap_build("input-keyboard", 64,
                                        &width, &height, &data, &data_len));
    ASSERT(width == 64);
    ASSERT(height == 64);
    ASSERT(data != nullptr);
    ASSERT(data_len == 64 * 64 * 4);
    ASSERT(alpha_sum(data, data_len) > 0);

    typio_tray_icon_pixmap_free(data);
}

TEST(build_rime_icon) {
    unsigned char *data = nullptr;
    int width = 0;
    int height = 0;
    int data_len = 0;

    ASSERT(typio_tray_icon_pixmap_build("typio-rime", 64,
                                        &width, &height, &data, &data_len));
    ASSERT(width == 64);
    ASSERT(height == 64);
    ASSERT(data != nullptr);
    ASSERT(data_len == 64 * 64 * 4);
    ASSERT(alpha_sum(data, data_len) > 0);

    typio_tray_icon_pixmap_free(data);
}

int main(void) {
    printf("Running tray icon pixmap tests:\n");
    run_test_build_basic_icon();
    run_test_build_rime_icon();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
