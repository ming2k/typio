/**
 * @file test_startup_health.c
 * @brief Startup health check tests
 */

#include "health.h"
#include "typio/typio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)

static void write_text_file(const char *path, const char *content) {
    FILE *file = fopen(path, "w");
    ASSERT(file != NULL);
    ASSERT(fputs(content, file) >= 0);
    ASSERT(fclose(file) == 0);
}

static void make_dir(const char *path) {
    ASSERT(mkdir(path, 0700) == 0);
}

static void remove_path_if_exists(const char *path) {
    if (rmdir(path) == 0) {
        return;
    }
    if (unlink(path) == 0) {
        return;
    }
}

static void cleanup_tree(const char *root) {
    char config_path[512];
    char config_dir[512];
    char data_dir[512];
    char engines_dir[512];

    ASSERT(snprintf(config_path, sizeof(config_path), "%s/config/typio.toml", root) > 0);
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", root) > 0);
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", root) > 0);
    ASSERT(snprintf(engines_dir, sizeof(engines_dir), "%s/data/engines", root) > 0);

    remove_path_if_exists(config_path);
    remove_path_if_exists(engines_dir);
    remove_path_if_exists(config_dir);
    remove_path_if_exists(data_dir);
    ASSERT(rmdir(root) == 0);
}

static TypioInstance *make_instance_with_config(const char *config_text,
                                                char *root,
                                                size_t root_size) {
    char template_path[] = "/tmp/typio-startup-health-XXXXXX";
    char config_dir[512];
    char data_dir[512];
    char config_path[512];
    TypioInstanceConfig config = {};
    TypioInstance *instance;

    ASSERT(mkdtemp(template_path) != NULL);
    ASSERT(snprintf(root, root_size, "%s", template_path) > 0);
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", root) > 0);
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", root) > 0);
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config_dir) > 0);

    make_dir(config_dir);
    make_dir(data_dir);
    write_text_file(config_path, config_text);

    config.config_dir = config_dir;
    config.data_dir = data_dir;

    instance = typio_instance_new_with_config(&config);
    ASSERT(instance != NULL);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);
    return instance;
}

static const TypioEngineInfo mock_voice_info = {
    .name = "mock-voice",
    .display_name = "Mock Voice",
    .description = "Mock voice engine",
    .version = "1.0.0",
    .author = "Test",
    .icon = NULL,
    .language = NULL,
    .type = TYPIO_ENGINE_TYPE_VOICE,
    .capabilities = TYPIO_CAP_VOICE_INPUT,
    .api_version = TYPIO_API_VERSION,
};

static const TypioEngineInfo *mock_voice_get_info(void) {
    return &mock_voice_info;
}

static char *mock_voice_process_audio([[maybe_unused]] TypioEngine *engine,
                                       [[maybe_unused]] const float *samples,
                                       [[maybe_unused]] size_t n_samples) {
    return NULL;
}

static const TypioVoiceEngineOps mock_voice_ops = {
    .process_audio = mock_voice_process_audio,
};

static TypioEngine *mock_voice_create(void) {
    static const TypioEngineBaseOps ops = {};
    return typio_engine_new(&mock_voice_info, &ops, NULL, &mock_voice_ops);
}

TEST(missing_default_engine_warns) {
    char root[512];
    TypioStartupIssue issues[4];
    TypioInstance *instance = make_instance_with_config(
        "default_engine = \"missing\"\n",
        root, sizeof(root));

    size_t count = typio_startup_health_collect(instance, issues, 4);
    ASSERT_EQ(count, 1U);
    ASSERT_STR_EQ(issues[0].code, "default-engine-missing");

    typio_instance_free(instance);
    cleanup_tree(root);
}

TEST(voice_engine_not_activated_warns) {
    char root[512];
    TypioStartupIssue issues[4];
    TypioInstance *instance = make_instance_with_config(
        "default_voice_engine = \"mock-voice\"\n",
        root, sizeof(root));
    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    ASSERT_EQ(typio_engine_manager_register(manager,
                                            mock_voice_create,
                                            mock_voice_get_info),
              TYPIO_OK);

    size_t count = typio_startup_health_collect(instance, issues, 4);
    ASSERT_EQ(count, 1U);
    ASSERT(strcmp(issues[0].code, "voice-engine-not-active") == 0 ||
           strcmp(issues[0].code, "voice-support-not-built") == 0);

    typio_instance_free(instance);
    cleanup_tree(root);
}

TEST(startup_checks_can_be_disabled) {
    char root[512];
    TypioInstance *instance = make_instance_with_config(
        "default_engine = \"missing\"\n"
        "[notifications]\n"
        "startup_checks = false\n",
        root, sizeof(root));

    ASSERT(!typio_startup_checks_enabled(instance));

    typio_instance_free(instance);
    cleanup_tree(root);
}

int main(void) {
    printf("Running startup health tests:\n");

    run_test_missing_default_engine_warns();
    run_test_voice_engine_not_activated_warns();
    run_test_startup_checks_can_be_disabled();

    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
