/**
 * @file test_engine_manager.c
 * @brief Engine manager tests
 */

#include "typio/typio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;
static int mock_focus_in_count = 0;
static int mock_focus_out_count = 0;
static int mock2_focus_in_count = 0;
static int mock2_focus_out_count = 0;

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

static size_t engine_count(TypioEngineManager *manager) {
    size_t count = 0;
    const char **engines = typio_engine_manager_list(manager, &count);
    ASSERT_NOT_NULL(engines);
    return count;
}

static void ensure_dir(const char *path) {
    ASSERT(path != NULL);
    ASSERT(mkdir(path, 0755) == 0 || access(path, F_OK) == 0);
}

static void sleep_past_switch_threshold(void) {
    struct timespec ts = {
        .tv_sec = 1,
        .tv_nsec = 100 * 1000 * 1000,
    };
    nanosleep(&ts, NULL);
}

static TypioInstance *create_temp_instance(char *root_template,
                                           TypioInstanceConfig *config_out) {
    static char config_dir[1024];
    static char data_dir[1024];
    static char state_dir[1024];
    static char engine_dir[1024];

    ASSERT(mkdtemp(root_template) != NULL);
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", root_template) < (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", root_template) < (int)sizeof(data_dir));
    ASSERT(snprintf(state_dir, sizeof(state_dir), "%s/state", root_template) < (int)sizeof(state_dir));
    ASSERT(snprintf(engine_dir, sizeof(engine_dir), "%s/engines", root_template) < (int)sizeof(engine_dir));

    ensure_dir(config_dir);
    ensure_dir(data_dir);
    ensure_dir(state_dir);
    ensure_dir(engine_dir);

    memset(config_out, 0, sizeof(*config_out));
    config_out->config_dir = config_dir;
    config_out->data_dir = data_dir;
    config_out->state_dir = state_dir;
    config_out->engine_dir = engine_dir;
    return typio_instance_new_with_config(config_out);
}

/* Mock engine for testing */
static TypioResult mock_init(TypioEngine *engine, [[maybe_unused]] TypioInstance *instance) {
    engine->active = true;
    return TYPIO_OK;
}

static void mock_destroy([[maybe_unused]] TypioEngine *engine) {
}

static TypioKeyProcessResult mock_process_key([[maybe_unused]] TypioEngine *engine, [[maybe_unused]] TypioInputContext *ctx,
                              [[maybe_unused]] const TypioKeyEvent *event) {
    return TYPIO_KEY_NOT_HANDLED;
}

static void mock_focus_in(TypioEngine *engine, [[maybe_unused]] TypioInputContext *ctx) {
    if (strcmp(typio_engine_get_name(engine), "mock") == 0) {
        mock_focus_in_count++;
    } else if (strcmp(typio_engine_get_name(engine), "mock2") == 0) {
        mock2_focus_in_count++;
    }
}

static void mock_focus_out(TypioEngine *engine, [[maybe_unused]] TypioInputContext *ctx) {
    if (strcmp(typio_engine_get_name(engine), "mock") == 0) {
        mock_focus_out_count++;
    } else if (strcmp(typio_engine_get_name(engine), "mock2") == 0) {
        mock2_focus_out_count++;
    }
}

static const TypioEngineInfo mock_engine_info = {
    .name = "mock",
    .display_name = "Mock Engine",
    .description = "A mock engine for testing",
    .version = "1.0.0",
    .author = "Test",
    .icon = "mock",
    .language = "en",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
    .capabilities = TYPIO_CAP_PREEDIT,
    .api_version = TYPIO_API_VERSION,
};

static const TypioEngineOps mock_engine_ops = {
    .init = mock_init,
    .destroy = mock_destroy,
    .focus_in = mock_focus_in,
    .focus_out = mock_focus_out,
    .process_key = mock_process_key,
};

static const TypioEngineInfo *mock_get_info(void) {
    return &mock_engine_info;
}

static TypioEngine *mock_create(void) {
    return typio_engine_new(&mock_engine_info, &mock_engine_ops);
}

/* Second mock engine */
static const TypioEngineInfo mock2_engine_info = {
    .name = "mock2",
    .display_name = "Mock Engine 2",
    .description = "Another mock engine",
    .version = "1.0.0",
    .author = "Test",
    .icon = "mock2",
    .language = "zh",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
    .capabilities = TYPIO_CAP_PREEDIT | TYPIO_CAP_CANDIDATES,
    .api_version = TYPIO_API_VERSION,
};

static const TypioEngineInfo *mock2_get_info(void) {
    return &mock2_engine_info;
}

static TypioEngine *mock2_create(void) {
    return typio_engine_new(&mock2_engine_info, &mock_engine_ops);
}

static TypioResult failing_init([[maybe_unused]] TypioEngine *engine,
                                [[maybe_unused]] TypioInstance *instance) {
    return TYPIO_ERROR;
}

static const TypioEngineOps failing_engine_ops = {
    .init = failing_init,
    .destroy = mock_destroy,
    .process_key = mock_process_key,
};

static const TypioEngineInfo failing_engine_info = {
    .name = "failing",
    .display_name = "Failing Engine",
    .description = "An engine that fails activation",
    .version = "1.0.0",
    .author = "Test",
    .icon = "failing",
    .language = "en",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
    .capabilities = TYPIO_CAP_PREEDIT,
    .api_version = TYPIO_API_VERSION,
};

static const TypioEngineInfo *failing_get_info(void) {
    return &failing_engine_info;
}

static TypioEngine *failing_create(void) {
    return typio_engine_new(&failing_engine_info, &failing_engine_ops);
}

/* Mock voice engine */
static const TypioEngineInfo mock_voice_info = {
    .name = "mock-voice",
    .display_name = "Mock Voice Engine",
    .description = "A mock voice engine for testing",
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

static TypioEngine *mock_voice_create(void) {
    return typio_engine_new(&mock_voice_info, &mock_engine_ops);
}

static const TypioEngineInfo failing_voice_info = {
    .name = "failing-voice",
    .display_name = "Failing Voice Engine",
    .description = "A voice engine that fails activation",
    .version = "1.0.0",
    .author = "Test",
    .icon = NULL,
    .language = NULL,
    .type = TYPIO_ENGINE_TYPE_VOICE,
    .capabilities = TYPIO_CAP_VOICE_INPUT,
    .api_version = TYPIO_API_VERSION,
};

static const TypioEngineInfo *failing_voice_get_info(void) {
    return &failing_voice_info;
}

static TypioEngine *failing_voice_create(void) {
    return typio_engine_new(&failing_voice_info, &failing_engine_ops);
}

/* Test: Engine manager creation */
TEST(manager_create) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);
    ASSERT_NOT_NULL(manager);

    typio_instance_free(instance);
}

/* Test: Register engine */
TEST(register_engine) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    TypioResult result = typio_engine_manager_register(manager, mock_create, mock_get_info);
    ASSERT_EQ(result, TYPIO_OK);

    /* Try to register again - should fail */
    result = typio_engine_manager_register(manager, mock_create, mock_get_info);
    ASSERT_EQ(result, TYPIO_ERROR_ALREADY_EXISTS);

    typio_instance_free(instance);
}

/* Test: List engines */
TEST(list_engines) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, mock2_create, mock2_get_info);

    size_t count;
    const char **engines = typio_engine_manager_list(manager, &count);

    ASSERT(count >= 3);
    ASSERT_NOT_NULL(engines);

    /* Check that both engines are listed */
    bool found_mock = false;
    bool found_mock2 = false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(engines[i], "mock") == 0) found_mock = true;
        if (strcmp(engines[i], "mock2") == 0) found_mock2 = true;
    }
    ASSERT(found_mock);
    ASSERT(found_mock2);

    typio_instance_free(instance);
}

TEST(list_engines_by_type) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, mock_voice_create, mock_voice_get_info);

    size_t keyboard_count = 0;
    size_t voice_count = 0;
    const char **keyboard_engines =
        typio_engine_manager_list_by_type(manager, TYPIO_ENGINE_TYPE_KEYBOARD, &keyboard_count);
    const char **voice_engines =
        typio_engine_manager_list_by_type(manager, TYPIO_ENGINE_TYPE_VOICE, &voice_count);

    ASSERT_NOT_NULL(keyboard_engines);
    ASSERT_NOT_NULL(voice_engines);
    ASSERT(keyboard_count >= 2);
    ASSERT_EQ(voice_count, 1);
    ASSERT_STR_EQ(voice_engines[0], "mock-voice");

    free((void *)keyboard_engines);
    free((void *)voice_engines);
    typio_instance_free(instance);
}

/* Test: Get engine info */
TEST(get_info) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_create, mock_get_info);

    const TypioEngineInfo *info = typio_engine_manager_get_info(manager, "mock");
    ASSERT_NOT_NULL(info);
    ASSERT_STR_EQ(info->name, "mock");
    ASSERT_STR_EQ(info->display_name, "Mock Engine");

    /* Non-existent engine */
    const TypioEngineInfo *missing = typio_engine_manager_get_info(manager, "nonexistent");
    ASSERT_NULL(missing);

    typio_instance_free(instance);
}

/* Test: Activate engine */
TEST(activate_engine) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_create, mock_get_info);

    /* Built-in engine should be active by default */
    TypioEngine *active = typio_engine_manager_get_active(manager);
    ASSERT_NOT_NULL(active);
    ASSERT_STR_EQ(typio_engine_get_name(active), "basic");

    /* Activate mock engine */
    TypioResult result = typio_engine_manager_set_active(manager, "mock");
    ASSERT_EQ(result, TYPIO_OK);

    active = typio_engine_manager_get_active(manager);
    ASSERT_NOT_NULL(active);
    ASSERT_STR_EQ(typio_engine_get_name(active), "mock");
    ASSERT(typio_engine_is_active(active));

    typio_instance_free(instance);
}

TEST(keyboard_and_voice_slots_are_independent) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, mock_voice_create, mock_voice_get_info);

    ASSERT_EQ(typio_engine_manager_set_active(manager, "mock"), TYPIO_OK);
    ASSERT_EQ(typio_engine_manager_set_active_voice(manager, "mock-voice"), TYPIO_OK);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "mock");
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active_voice(manager)),
                  "mock-voice");
    ASSERT_EQ(typio_engine_manager_get_active_by_type(manager, TYPIO_ENGINE_TYPE_KEYBOARD),
              typio_engine_manager_get_active(manager));
    ASSERT_EQ(typio_engine_manager_get_active_by_type(manager, TYPIO_ENGINE_TYPE_VOICE),
              typio_engine_manager_get_active_voice(manager));

    typio_instance_free(instance);
}

/* Test: Switch engines */
TEST(switch_engines) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, mock2_create, mock2_get_info);

    /* Activate first engine */
    typio_engine_manager_set_active(manager, "mock");
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "mock");

    /* Switch to second engine */
    typio_engine_manager_set_active(manager, "mock2");
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "mock2");

    /* Cycle with next() */
    typio_engine_manager_next(manager);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "basic");

    typio_engine_manager_next(manager);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "mock");

    /* Cycle with prev() */
    typio_engine_manager_prev(manager);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "basic");

    typio_instance_free(instance);
}

TEST(switch_engine_rebinds_focused_context) {
    TypioInstance *instance = typio_instance_new();
    TypioInputContext *ctx;
    TypioEngineManager *manager;

    mock_focus_in_count = 0;
    mock_focus_out_count = 0;
    mock2_focus_in_count = 0;
    mock2_focus_out_count = 0;

    typio_instance_init(instance);
    manager = typio_instance_get_engine_manager(instance);
    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, mock2_create, mock2_get_info);

    ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);
    typio_input_context_focus_in(ctx);

    ASSERT_EQ(typio_engine_manager_set_active(manager, "mock"), TYPIO_OK);
    ASSERT_EQ(mock_focus_in_count, 1);
    ASSERT_EQ(mock_focus_out_count, 0);

    ASSERT_EQ(typio_engine_manager_set_active(manager, "mock2"), TYPIO_OK);
    ASSERT_EQ(mock_focus_out_count, 1);
    ASSERT_EQ(mock2_focus_in_count, 1);
    ASSERT_EQ(mock2_focus_out_count, 0);

    typio_instance_free(instance);
}

TEST(failed_keyboard_switch_restores_previous_engine) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, failing_create, failing_get_info);

    ASSERT_EQ(typio_engine_manager_set_active(manager, "mock"), TYPIO_OK);
    TypioEngine *active = typio_engine_manager_get_active(manager);
    ASSERT_NOT_NULL(active);
    ASSERT_STR_EQ(typio_engine_get_name(active), "mock");
    ASSERT(typio_engine_is_active(active));

    ASSERT_EQ(typio_engine_manager_set_active(manager, "failing"), TYPIO_ERROR);
    active = typio_engine_manager_get_active(manager);
    ASSERT_NOT_NULL(active);
    ASSERT_STR_EQ(typio_engine_get_name(active), "mock");
    ASSERT(typio_engine_is_active(active));

    typio_instance_free(instance);
}

TEST(failed_voice_switch_restores_previous_engine) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_voice_create, mock_voice_get_info);
    typio_engine_manager_register(manager, failing_voice_create, failing_voice_get_info);

    ASSERT_EQ(typio_engine_manager_set_active_voice(manager, "mock-voice"), TYPIO_OK);
    TypioEngine *active_voice = typio_engine_manager_get_active_voice(manager);
    ASSERT_NOT_NULL(active_voice);
    ASSERT_STR_EQ(typio_engine_get_name(active_voice), "mock-voice");
    ASSERT(typio_engine_is_active(active_voice));

    ASSERT_EQ(typio_engine_manager_set_active_voice(manager, "failing-voice"), TYPIO_ERROR);
    active_voice = typio_engine_manager_get_active_voice(manager);
    ASSERT_NOT_NULL(active_voice);
    ASSERT_STR_EQ(typio_engine_get_name(active_voice), "mock-voice");
    ASSERT(typio_engine_is_active(active_voice));

    typio_instance_free(instance);
}

/* Test: Unload engine */
TEST(unload_engine) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, mock2_create, mock2_get_info);

    size_t count_before = engine_count(manager);
    ASSERT(count_before >= 3);

    /* Unload mock engine */
    TypioResult result = typio_engine_manager_unload(manager, "mock");
    ASSERT_EQ(result, TYPIO_OK);

    size_t count_after = engine_count(manager);
    ASSERT_EQ(count_after + 1, count_before);

    /* Try to unload non-existent */
    result = typio_engine_manager_unload(manager, "mock");
    ASSERT_EQ(result, TYPIO_ERROR_NOT_FOUND);

    typio_instance_free(instance);
}

/* Test: Engine capabilities */
TEST(engine_capabilities) {
    TypioEngine *engine = mock_create();
    ASSERT_NOT_NULL(engine);

    ASSERT(typio_engine_has_capability(engine, TYPIO_CAP_PREEDIT));
    ASSERT(!typio_engine_has_capability(engine, TYPIO_CAP_CANDIDATES));
    ASSERT(!typio_engine_has_capability(engine, TYPIO_CAP_VOICE_INPUT));

    ASSERT_EQ(typio_engine_get_type(engine), TYPIO_ENGINE_TYPE_KEYBOARD);

    typio_engine_free(engine);

    /* Test mock2 with more capabilities */
    engine = mock2_create();
    ASSERT(typio_engine_has_capability(engine, TYPIO_CAP_PREEDIT));
    ASSERT(typio_engine_has_capability(engine, TYPIO_CAP_CANDIDATES));

    typio_engine_free(engine);
}

/* Test: Engine user data */
TEST(engine_user_data) {
    TypioEngine *engine = mock_create();
    ASSERT_NOT_NULL(engine);

    ASSERT_NULL(typio_engine_get_user_data(engine));

    int *data = malloc(sizeof(int));
    *data = 12345;
    typio_engine_set_user_data(engine, data);

    int *retrieved = typio_engine_get_user_data(engine);
    ASSERT_NOT_NULL(retrieved);
    ASSERT_EQ(*retrieved, 12345);

    free(data);
    typio_engine_free(engine);
}

/* Test: Voice engine activation uses separate slot */
TEST(voice_engine_activation) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, mock_voice_create, mock_voice_get_info);

    /* Activate keyboard engine */
    typio_engine_manager_set_active(manager, "mock");
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "mock");

    /* No voice engine active yet */
    ASSERT_NULL(typio_engine_manager_get_active_voice(manager));

    /* Activate voice engine - should not change keyboard slot */
    TypioResult result = typio_engine_manager_set_active_voice(manager, "mock-voice");
    ASSERT_EQ(result, TYPIO_OK);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "mock");
    ASSERT_NOT_NULL(typio_engine_manager_get_active_voice(manager));
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active_voice(manager)), "mock-voice");

    /* set_active with voice engine name should also route to voice slot */
    typio_engine_manager_set_active(manager, "mock-voice");
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "mock");

    /* set_active_voice with keyboard engine should fail */
    result = typio_engine_manager_set_active_voice(manager, "mock");
    ASSERT_EQ(result, TYPIO_ERROR_INVALID_ARGUMENT);

    typio_instance_free(instance);
}

/* Test: next/prev skip voice engines */
TEST(next_prev_skip_voice) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, mock_voice_create, mock_voice_get_info);
    typio_engine_manager_register(manager, mock2_create, mock2_get_info);

    /* Start at mock, cycle forward: should go mock → mock2 → basic → mock,
     * never landing on mock-voice */
    typio_engine_manager_set_active(manager, "mock");

    typio_engine_manager_next(manager);
    const char *name = typio_engine_get_name(typio_engine_manager_get_active(manager));
    ASSERT(strcmp(name, "mock-voice") != 0);

    typio_engine_manager_next(manager);
    name = typio_engine_get_name(typio_engine_manager_get_active(manager));
    ASSERT(strcmp(name, "mock-voice") != 0);

    typio_engine_manager_next(manager);
    name = typio_engine_get_name(typio_engine_manager_get_active(manager));
    ASSERT(strcmp(name, "mock-voice") != 0);

    /* Cycle backward similarly */
    typio_engine_manager_prev(manager);
    name = typio_engine_get_name(typio_engine_manager_get_active(manager));
    ASSERT(strcmp(name, "mock-voice") != 0);

    typio_engine_manager_prev(manager);
    name = typio_engine_get_name(typio_engine_manager_get_active(manager));
    ASSERT(strcmp(name, "mock-voice") != 0);

    typio_instance_free(instance);
}

TEST(explicit_engine_order_controls_listing_and_switching) {
    char root[] = "/tmp/typio-engine-order-XXXXXX";
    TypioInstanceConfig config;
    TypioInstance *instance = create_temp_instance(root, &config);
    TypioEngineManager *manager;
    TypioConfig *instance_config;
    const char *order[] = {"mock2", "basic", "mock"};
    const char **engines;
    size_t count = 0;

    typio_instance_init(instance);
    manager = typio_instance_get_engine_manager(instance);
    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, mock2_create, mock2_get_info);

    instance_config = typio_instance_get_config(instance);
    ASSERT_NOT_NULL(instance_config);
    ASSERT_EQ(typio_config_set_string_array(instance_config, "engine_order", order, 3), TYPIO_OK);

    engines = typio_engine_manager_list_ordered_keyboards(manager, &count);
    ASSERT(count >= 3);
    ASSERT_STR_EQ(engines[0], "mock2");
    ASSERT_STR_EQ(engines[1], "basic");
    ASSERT_STR_EQ(engines[2], "mock");

    ASSERT_EQ(typio_engine_manager_set_active(manager, "mock2"), TYPIO_OK);
    ASSERT_EQ(typio_engine_manager_next(manager), TYPIO_OK);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "basic");
    ASSERT_EQ(typio_engine_manager_next(manager), TYPIO_OK);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "mock");
    ASSERT_EQ(typio_engine_manager_prev(manager), TYPIO_OK);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "basic");

    typio_instance_free(instance);
}

TEST(instance_reload_preserves_default_engine_and_rime_schema) {
    char root[] = "/tmp/typio-instance-reload-XXXXXX";
    TypioInstanceConfig config;
    TypioInstance *instance = create_temp_instance(root, &config);
    char config_path[1024];
    FILE *file;
    char *rendered;

    ASSERT_NOT_NULL(instance);
    ASSERT(snprintf(config_path, sizeof(config_path), "%s/typio.toml", config.config_dir) <
           (int)sizeof(config_path));

    file = fopen(config_path, "w");
    ASSERT_NOT_NULL(file);
    ASSERT(fputs(
        "default_engine = \"rime\"\n"
        "\n"
        "[engines]\n"
        "rime.popup_theme = \"auto\"\n"
        "rime.schema = \"m2k_pinyin\"\n",
        file) >= 0);
    fclose(file);

    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);
    ASSERT_EQ(typio_instance_reload_config(instance), TYPIO_OK);

    rendered = typio_instance_get_config_text(instance);
    ASSERT_NOT_NULL(rendered);
    ASSERT(strstr(rendered, "default_engine = \"rime\"") != NULL);
    ASSERT(strstr(rendered, "rime.schema = \"m2k_pinyin\"") != NULL);
    free(rendered);

    typio_instance_free(instance);
}

TEST(stable_pair_prefers_recent_engines_and_skips_rapid_persistence) {
    char root[] = "/tmp/typio-engine-mru-XXXXXX";
    TypioInstanceConfig config;
    TypioInstance *instance = create_temp_instance(root, &config);
    TypioEngineManager *manager;

    typio_instance_init(instance);
    manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, mock2_create, mock2_get_info);

    /* Activate mock and commit to register it in the recent pair */
    ASSERT_EQ(typio_engine_manager_set_active(manager, "mock"), TYPIO_OK);
    typio_engine_manager_notify_commit(manager);

    /* Activate mock2 and commit to register it as primary */
    ASSERT_EQ(typio_engine_manager_set_active(manager, "mock2"), TYPIO_OK);
    typio_engine_manager_notify_commit(manager);

    /* Slow switch: should toggle to recent partner (mock) */
    sleep_past_switch_threshold();
    ASSERT_EQ(typio_engine_manager_next(manager), TYPIO_OK);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "mock");

    /* Rapid next: should cycle (no slow-switch toggle) */
    ASSERT_EQ(typio_engine_manager_next(manager), TYPIO_OK);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "mock2");

    ASSERT_EQ(typio_engine_manager_next(manager), TYPIO_OK);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "basic");

    typio_instance_free(instance);
}

TEST(stable_pair_persists_across_instances) {
    char root[] = "/tmp/typio-engine-state-XXXXXX";
    TypioInstanceConfig config;
    TypioInstance *instance = create_temp_instance(root, &config);
    TypioEngineManager *manager;

    typio_instance_init(instance);
    manager = typio_instance_get_engine_manager(instance);
    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, mock2_create, mock2_get_info);

    /* Build the recent pair through commits */
    ASSERT_EQ(typio_engine_manager_set_active(manager, "mock"), TYPIO_OK);
    typio_engine_manager_notify_commit(manager);
    ASSERT_EQ(typio_engine_manager_set_active(manager, "mock2"), TYPIO_OK);
    typio_engine_manager_notify_commit(manager);

    /* Slow-switch should toggle to mock */
    sleep_past_switch_threshold();
    ASSERT_EQ(typio_engine_manager_next(manager), TYPIO_OK);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "mock");
    typio_instance_free(instance);

    /* Second instance should reload the persisted pair */
    instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);
    typio_instance_init(instance);
    manager = typio_instance_get_engine_manager(instance);
    typio_engine_manager_register(manager, mock_create, mock_get_info);
    typio_engine_manager_register(manager, mock2_create, mock2_get_info);

    ASSERT_EQ(typio_engine_manager_set_active(manager, "mock2"), TYPIO_OK);
    sleep_past_switch_threshold();
    ASSERT_EQ(typio_engine_manager_next(manager), TYPIO_OK);
    ASSERT_STR_EQ(typio_engine_get_name(typio_engine_manager_get_active(manager)), "mock");

    typio_instance_free(instance);
}

/* Test: Unload voice engine clears voice slot */
TEST(unload_voice_engine) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);

    typio_engine_manager_register(manager, mock_voice_create, mock_voice_get_info);

    typio_engine_manager_set_active_voice(manager, "mock-voice");
    ASSERT_NOT_NULL(typio_engine_manager_get_active_voice(manager));

    typio_engine_manager_unload(manager, "mock-voice");
    ASSERT_NULL(typio_engine_manager_get_active_voice(manager));

    typio_instance_free(instance);
}

int main(void) {
    printf("Running engine manager tests:\n");

    run_test_manager_create();
    run_test_register_engine();
    run_test_list_engines();
    run_test_list_engines_by_type();
    run_test_get_info();
    run_test_activate_engine();
    run_test_keyboard_and_voice_slots_are_independent();
    run_test_switch_engines();
    run_test_switch_engine_rebinds_focused_context();
    run_test_failed_keyboard_switch_restores_previous_engine();
    run_test_failed_voice_switch_restores_previous_engine();
    run_test_unload_engine();
    run_test_engine_capabilities();
    run_test_engine_user_data();
    run_test_voice_engine_activation();
    run_test_next_prev_skip_voice();
    run_test_explicit_engine_order_controls_listing_and_switching();
    run_test_instance_reload_preserves_default_engine_and_rime_schema();
    run_test_unload_voice_engine();
    run_test_stable_pair_prefers_recent_engines_and_skips_rapid_persistence();
    run_test_stable_pair_persists_across_instances();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
