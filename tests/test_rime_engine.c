
#include "typio/typio.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef TYPIO_TEST_RIME_ENGINE_DIR
#error "TYPIO_TEST_RIME_ENGINE_DIR must be defined"
#endif

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
#define ASSERT_NOT_NULL(a) ASSERT((a) != nullptr)
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)

typedef struct CaptureState {
    char *commit_text;
    char *preedit_text;
    size_t candidate_count;
} CaptureState;

static void free_capture(CaptureState *capture) {
    if (!capture) {
        return;
    }
    free(capture->commit_text);
    free(capture->preedit_text);
    memset(capture, 0, sizeof(*capture));
}

static bool ensure_dir(const char *path) {
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static bool write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "w");
    if (!file) {
        return false;
    }
    fputs(content, file);
    fclose(file);
    return true;
}

static void capture_commit([[maybe_unused]] TypioInputContext *ctx, const char *text, void *user_data) {
    CaptureState *capture = user_data;

    free(capture->commit_text);
    capture->commit_text = text ? strdup(text) : nullptr;
}

static void capture_preedit([[maybe_unused]] TypioInputContext *ctx,
                            const TypioPreedit *preedit,
                            void *user_data) {
    CaptureState *capture = user_data;
    size_t total = 0;
    char *buffer;

    free(capture->preedit_text);
    capture->preedit_text = nullptr;

    if (!preedit || preedit->segment_count == 0) {
        return;
    }

    for (size_t i = 0; i < preedit->segment_count; ++i) {
        total += strlen(preedit->segments[i].text);
    }

    buffer = calloc(total + 1, 1);
    ASSERT_NOT_NULL(buffer);

    for (size_t i = 0; i < preedit->segment_count; ++i) {
        strcat(buffer, preedit->segments[i].text);
    }

    capture->preedit_text = buffer;
}

static void capture_candidates([[maybe_unused]] TypioInputContext *ctx,
                               const TypioCandidateList *candidates,
                               void *user_data) {
    CaptureState *capture = user_data;

    capture->candidate_count = candidates ? candidates->count : 0;
}

static TypioKeyEvent *key_event_for_char(char ch) {
    return typio_key_event_new(TYPIO_EVENT_KEY_PRESS,
                               (uint32_t)ch,
                               (uint32_t)ch,
                               TYPIO_MOD_NONE);
}

TEST(load_and_compose) {
    char temp_root[] = "/tmp/typio-rime-test-XXXXXX";
    char config_dir[1024];
    char data_dir[1024];
    char config_path[1024];
    TypioInstanceConfig config = {};
    CaptureState capture = {};

    ASSERT_NOT_NULL(mkdtemp(temp_root));

    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", temp_root) < (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", temp_root) < (int)sizeof(data_dir));
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config_dir) < (int)sizeof(config_path));

    ASSERT(ensure_dir(config_dir));
    ASSERT(ensure_dir(data_dir));
    ASSERT(write_file(config_path,
                      "default_engine = \"rime\"\n"
                      "[engines.rime]\n"
                      "schema = \"luna_pinyin\"\n"
                      "page_size = 5\n"));

    config.config_dir = config_dir;
    config.data_dir = data_dir;
    config.engine_dir = TYPIO_TEST_RIME_ENGINE_DIR;
    config.default_engine = "rime";

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);
    ASSERT_NOT_NULL(manager);
    TypioEngine *active = typio_engine_manager_get_active(manager);
    ASSERT_NOT_NULL(active);
    ASSERT_STR_EQ(typio_engine_get_name(active), "rime");

    TypioInputContext *ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);

    typio_input_context_set_commit_callback(ctx, capture_commit, &capture);
    typio_input_context_set_preedit_callback(ctx, capture_preedit, &capture);
    typio_input_context_set_candidate_callback(ctx, capture_candidates, &capture);
    typio_input_context_focus_in(ctx);

    TypioKeyEvent *n_key = key_event_for_char('n');
    TypioKeyEvent *i_key = key_event_for_char('i');
    TypioKeyEvent *space_key = key_event_for_char(' ');

    ASSERT_NOT_NULL(n_key);
    ASSERT_NOT_NULL(i_key);
    ASSERT_NOT_NULL(space_key);

    ASSERT(typio_input_context_process_key(ctx, n_key));
    ASSERT(typio_input_context_process_key(ctx, i_key));
    ASSERT(capture.candidate_count > 0 || (capture.preedit_text && *capture.preedit_text));

    ASSERT(typio_input_context_process_key(ctx, space_key));
    ASSERT(capture.commit_text && *capture.commit_text);

    typio_key_event_free(n_key);
    typio_key_event_free(i_key);
    typio_key_event_free(space_key);
    free_capture(&capture);
    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

int main(void) {
    printf("Running rime integration tests:\n");
    run_test_load_and_compose();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
