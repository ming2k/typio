#include "typio/rime_schema_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

static void ensure_dir(const char *path) {
    ASSERT(path != NULL);
    ASSERT(mkdir(path, 0755) == 0 || access(path, F_OK) == 0);
}

TEST(expands_user_data_dir_from_config) {
    char root[] = "/tmp/typio-rime-schema-list-XXXXXX";
    char user_dir[1024];
    char build_dir[1024];
    char default_yaml[1024];
    TypioConfig *config;
    TypioRimeSchemaList list;
    FILE *file;

    ASSERT(mkdtemp(root) != NULL);
    ASSERT(setenv("HOME", root, 1) == 0);
    ASSERT(snprintf(user_dir, sizeof(user_dir), "%s/rime-data", root) < (int)sizeof(user_dir));
    ASSERT(snprintf(build_dir, sizeof(build_dir), "%s/build", user_dir) < (int)sizeof(build_dir));
    ASSERT(snprintf(default_yaml, sizeof(default_yaml), "%s/default.yaml", build_dir) < (int)sizeof(default_yaml));
    ensure_dir(user_dir);
    ensure_dir(build_dir);

    file = fopen(default_yaml, "w");
    ASSERT(file != NULL);
    ASSERT(fputs("schema_list:\n  - schema: luna_pinyin\n", file) >= 0);
    fclose(file);

    config = typio_config_new();
    ASSERT(config != NULL);
    typio_config_set_string(config, "user_data_dir", "~/rime-data");
    typio_config_set_string(config, "schema", "luna_pinyin");

    ASSERT(typio_rime_schema_list_load(config, NULL, &list));
    ASSERT(list.user_data_dir != NULL);
    ASSERT_STR_EQ(list.user_data_dir, user_dir);
    ASSERT(list.schema_count == 1);
    ASSERT_STR_EQ(list.schemas[0].id, "luna_pinyin");
    ASSERT_STR_EQ(list.current_schema, "luna_pinyin");

    typio_rime_schema_list_clear(&list);
    typio_config_free(config);
}

int main(void) {
    printf("Running Rime schema list tests:\n");
    run_test_expands_user_data_dir_from_config();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
