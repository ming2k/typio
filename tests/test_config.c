/**
 * @file test_config.c
 * @brief Configuration system tests
 */

#include "typio/typio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_NULL(a) ASSERT((a) == nullptr)
#define ASSERT_NOT_NULL(a) ASSERT((a) != nullptr)
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)
#define ASSERT_FLOAT_EQ(a, b) ASSERT(((a) - (b)) < 0.001 && ((b) - (a)) < 0.001)

/* Test: Config creation */
TEST(config_create) {
    TypioConfig *config = typio_config_new();
    ASSERT_NOT_NULL(config);
    ASSERT_EQ(typio_config_key_count(config), 0);
    typio_config_free(config);
}

/* Test: String values */
TEST(config_string) {
    TypioConfig *config = typio_config_new();

    TypioResult result = typio_config_set_string(config, "name", "test");
    ASSERT_EQ(result, TYPIO_OK);

    const char *value = typio_config_get_string(config, "name", "default");
    ASSERT_STR_EQ(value, "test");

    /* Default value for missing key */
    const char *missing = typio_config_get_string(config, "missing", "default");
    ASSERT_STR_EQ(missing, "default");

    typio_config_free(config);
}

/* Test: Integer values */
TEST(config_int) {
    TypioConfig *config = typio_config_new();

    typio_config_set_int(config, "count", 42);

    int value = typio_config_get_int(config, "count", 0);
    ASSERT_EQ(value, 42);

    /* Default for missing */
    int missing = typio_config_get_int(config, "missing", -1);
    ASSERT_EQ(missing, -1);

    typio_config_free(config);
}

/* Test: Boolean values */
TEST(config_bool) {
    TypioConfig *config = typio_config_new();

    typio_config_set_bool(config, "enabled", true);
    typio_config_set_bool(config, "disabled", false);

    ASSERT(typio_config_get_bool(config, "enabled", false));
    ASSERT(!typio_config_get_bool(config, "disabled", true));
    ASSERT(typio_config_get_bool(config, "missing", true));

    typio_config_free(config);
}

/* Test: Float values */
TEST(config_float) {
    TypioConfig *config = typio_config_new();

    typio_config_set_float(config, "ratio", 3.14159);

    double value = typio_config_get_float(config, "ratio", 0.0);
    ASSERT_FLOAT_EQ(value, 3.14159);

    typio_config_free(config);
}

/* Test: Key operations */
TEST(config_keys) {
    TypioConfig *config = typio_config_new();

    typio_config_set_string(config, "key1", "value1");
    typio_config_set_int(config, "key2", 123);
    typio_config_set_bool(config, "key3", true);

    ASSERT_EQ(typio_config_key_count(config), 3);

    ASSERT(typio_config_has_key(config, "key1"));
    ASSERT(typio_config_has_key(config, "key2"));
    ASSERT(typio_config_has_key(config, "key3"));
    ASSERT(!typio_config_has_key(config, "key4"));

    /* Remove key */
    TypioResult result = typio_config_remove(config, "key2");
    ASSERT_EQ(result, TYPIO_OK);
    ASSERT_EQ(typio_config_key_count(config), 2);
    ASSERT(!typio_config_has_key(config, "key2"));

    typio_config_free(config);
}

/* Test: Section access */
TEST(config_section) {
    TypioConfig *config = typio_config_new();

    /* Set values with section prefix */
    typio_config_set_string(config, "engine.name", "rime");
    typio_config_set_int(config, "engine.timeout", 100);
    typio_config_set_bool(config, "engine.enabled", true);

    /* Get section */
    TypioConfig *section = typio_config_get_section(config, "engine");
    ASSERT_NOT_NULL(section);

    /* Check section values (without prefix) */
    ASSERT_STR_EQ(typio_config_get_string(section, "name", ""), "rime");
    ASSERT_EQ(typio_config_get_int(section, "timeout", 0), 100);
    ASSERT(typio_config_get_bool(section, "enabled", false));

    typio_config_free(section);
    typio_config_free(config);
}

/* Test: Config merge */
TEST(config_merge) {
    TypioConfig *config1 = typio_config_new();
    typio_config_set_string(config1, "name", "original");
    typio_config_set_int(config1, "value", 10);

    TypioConfig *config2 = typio_config_new();
    typio_config_set_string(config2, "name", "updated");
    typio_config_set_bool(config2, "new_key", true);

    TypioResult result = typio_config_merge(config1, config2);
    ASSERT_EQ(result, TYPIO_OK);

    /* Check merged values */
    ASSERT_STR_EQ(typio_config_get_string(config1, "name", ""), "updated");
    ASSERT_EQ(typio_config_get_int(config1, "value", 0), 10);
    ASSERT(typio_config_get_bool(config1, "new_key", false));

    typio_config_free(config1);
    typio_config_free(config2);
}

/* Test: Value overwrite */
TEST(config_overwrite) {
    TypioConfig *config = typio_config_new();

    typio_config_set_string(config, "key", "first");
    ASSERT_STR_EQ(typio_config_get_string(config, "key", ""), "first");

    typio_config_set_string(config, "key", "second");
    ASSERT_STR_EQ(typio_config_get_string(config, "key", ""), "second");

    /* Count should still be 1 */
    ASSERT_EQ(typio_config_key_count(config), 1);

    typio_config_free(config);
}

/* Test: Load from string */
TEST(config_load_string) {
    const char *content =
        "# Comment\n"
        "name = test\n"
        "count = 42\n"
        "enabled = true\n"
        "\n"
        "[section]\n"
        "key = value\n";

    TypioConfig *config = typio_config_load_string(content);
    ASSERT_NOT_NULL(config);

    ASSERT_STR_EQ(typio_config_get_string(config, "name", ""), "test");
    ASSERT_EQ(typio_config_get_int(config, "count", 0), 42);
    ASSERT(typio_config_get_bool(config, "enabled", false));
    ASSERT_STR_EQ(typio_config_get_string(config, "section.key", ""), "value");

    typio_config_free(config);
}

/* Test: Save and load */
TEST(config_save_load) {
    TypioConfig *config = typio_config_new();
    typio_config_set_string(config, "name", "test_save");
    typio_config_set_int(config, "number", 999);

    const char *path = "/tmp/typio_test_config.conf";

    /* Save */
    TypioResult result = typio_config_save_file(config, path);
    ASSERT_EQ(result, TYPIO_OK);

    typio_config_free(config);

    /* Load */
    config = typio_config_load_file(path);
    ASSERT_NOT_NULL(config);

    ASSERT_STR_EQ(typio_config_get_string(config, "name", ""), "test_save");
    ASSERT_EQ(typio_config_get_int(config, "number", 0), 999);

    typio_config_free(config);

    /* Cleanup */
    remove(path);
}

int main(void) {
    printf("Running config tests:\n");

    run_test_config_create();
    run_test_config_string();
    run_test_config_int();
    run_test_config_bool();
    run_test_config_float();
    run_test_config_keys();
    run_test_config_section();
    run_test_config_merge();
    run_test_config_overwrite();
    run_test_config_load_string();
    run_test_config_save_load();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
