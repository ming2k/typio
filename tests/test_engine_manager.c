/**
 * @file test_engine_manager.c
 * @brief Engine manager tests
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

static size_t engine_count(TypioEngineManager *manager) {
    size_t count = 0;
    const char **engines = typio_engine_manager_list(manager, &count);
    ASSERT_NOT_NULL(engines);
    return count;
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

int main(void) {
    printf("Running engine manager tests:\n");

    run_test_manager_create();
    run_test_register_engine();
    run_test_list_engines();
    run_test_get_info();
    run_test_activate_engine();
    run_test_switch_engines();
    run_test_unload_engine();
    run_test_engine_capabilities();
    run_test_engine_user_data();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
