/**
 * @file test_core.c
 * @brief Core library tests
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
#define ASSERT_NULL(a) ASSERT((a) == NULL)
#define ASSERT_NOT_NULL(a) ASSERT((a) != NULL)
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)

static void capture_commit(TypioInputContext *ctx, const char *text, void *user_data) {
    (void)ctx;

    char **slot = user_data;
    size_t len = strlen(text);
    char *copy = malloc(len + 1);
    ASSERT_NOT_NULL(copy);
    memcpy(copy, text, len + 1);

    free(*slot);
    *slot = copy;
}

/* Test: Instance creation */
TEST(instance_create) {
    TypioInstance *instance = typio_instance_new();
    ASSERT_NOT_NULL(instance);
    typio_instance_free(instance);
}

/* Test: Instance with config */
TEST(instance_config) {
    TypioInstanceConfig config = {
        .config_dir = "/tmp/typio_test_config",
        .data_dir = "/tmp/typio_test_data",
    };

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);

    ASSERT_STR_EQ(typio_instance_get_config_dir(instance), "/tmp/typio_test_config");
    ASSERT_STR_EQ(typio_instance_get_data_dir(instance), "/tmp/typio_test_data");

    typio_instance_free(instance);
}

/* Test: Instance initialization */
TEST(instance_init) {
    TypioInstanceConfig config = {
        .config_dir = "/tmp/typio_test",
        .data_dir = "/tmp/typio_test_data",
    };

    TypioInstance *instance = typio_instance_new_with_config(&config);
    ASSERT_NOT_NULL(instance);

    TypioResult result = typio_instance_init(instance);
    ASSERT_EQ(result, TYPIO_OK);

    /* Engine manager should be available */
    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);
    ASSERT_NOT_NULL(manager);

    /* Built-in basic engine should be active */
    TypioEngine *active = typio_engine_manager_get_active(manager);
    ASSERT_NOT_NULL(active);
    ASSERT_STR_EQ(typio_engine_get_name(active), "basic");

    typio_instance_free(instance);
}

/* Test: Input context creation */
TEST(context_create) {
    TypioInstance *instance = typio_instance_new();
    ASSERT_NOT_NULL(instance);

    typio_instance_init(instance);

    TypioInputContext *ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);

    /* Should not be focused initially */
    ASSERT(!typio_input_context_is_focused(ctx));

    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

/* Test: Input context focus */
TEST(context_focus) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioInputContext *ctx = typio_instance_create_context(instance);

    typio_input_context_focus_in(ctx);
    ASSERT(typio_input_context_is_focused(ctx));
    ASSERT_EQ(typio_instance_get_focused_context(instance), ctx);

    typio_input_context_focus_out(ctx);
    ASSERT(!typio_input_context_is_focused(ctx));
    ASSERT_NULL(typio_instance_get_focused_context(instance));

    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

/* Test: Input context properties */
TEST(context_properties) {
    TypioInstance *instance = typio_instance_new();
    typio_instance_init(instance);

    TypioInputContext *ctx = typio_instance_create_context(instance);

    /* Set property */
    int *value = malloc(sizeof(int));
    *value = 42;
    typio_input_context_set_property(ctx, "test_prop", value, free);

    /* Get property */
    int *retrieved = typio_input_context_get_property(ctx, "test_prop");
    ASSERT_NOT_NULL(retrieved);
    ASSERT_EQ(*retrieved, 42);

    /* Non-existent property */
    void *missing = typio_input_context_get_property(ctx, "nonexistent");
    ASSERT_NULL(missing);

    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

/* Test: Built-in basic engine commits printable keys */
TEST(basic_engine_commit) {
    TypioInstance *instance = typio_instance_new();
    ASSERT_NOT_NULL(instance);
    ASSERT_EQ(typio_instance_init(instance), TYPIO_OK);

    TypioInputContext *ctx = typio_instance_create_context(instance);
    ASSERT_NOT_NULL(ctx);

    char *committed = NULL;
    typio_input_context_set_commit_callback(ctx, capture_commit, &committed);
    typio_input_context_focus_in(ctx);

    TypioKeyEvent *event = typio_key_event_new(
        TYPIO_EVENT_KEY_PRESS,
        38,
        'A',
        TYPIO_MOD_SHIFT
    );
    ASSERT_NOT_NULL(event);

    ASSERT(typio_input_context_process_key(ctx, event));
    ASSERT_NOT_NULL(committed);
    ASSERT_STR_EQ(committed, "A");

    typio_key_event_free(event);
    free(committed);
    typio_instance_destroy_context(instance, ctx);
    typio_instance_free(instance);
}

/* Test: Key event creation */
TEST(key_event) {
    TypioKeyEvent *event = typio_key_event_new(
        TYPIO_EVENT_KEY_PRESS,
        38,  /* keycode */
        'a', /* keysym */
        TYPIO_MOD_NONE
    );
    ASSERT_NOT_NULL(event);

    ASSERT(typio_key_event_is_press(event));
    ASSERT(!typio_key_event_is_release(event));
    ASSERT(!typio_key_event_has_modifier(event, TYPIO_MOD_SHIFT));
    ASSERT(!typio_key_event_is_modifier_only(event));

    typio_key_event_free(event);
}

/* Test: Key event with modifiers */
TEST(key_event_modifiers) {
    TypioKeyEvent *event = typio_key_event_new(
        TYPIO_EVENT_KEY_PRESS,
        38,
        'A',
        TYPIO_MOD_SHIFT | TYPIO_MOD_CTRL
    );
    ASSERT_NOT_NULL(event);

    ASSERT(typio_key_event_has_modifier(event, TYPIO_MOD_SHIFT));
    ASSERT(typio_key_event_has_modifier(event, TYPIO_MOD_CTRL));
    ASSERT(!typio_key_event_has_modifier(event, TYPIO_MOD_ALT));

    typio_key_event_free(event);
}

/* Test: Special key detection */
TEST(key_event_special) {
    TypioKeyEvent *bs = typio_key_event_new(TYPIO_EVENT_KEY_PRESS, 0, TYPIO_KEY_BackSpace, 0);
    ASSERT(typio_key_event_is_backspace(bs));
    typio_key_event_free(bs);

    TypioKeyEvent *enter = typio_key_event_new(TYPIO_EVENT_KEY_PRESS, 0, TYPIO_KEY_Return, 0);
    ASSERT(typio_key_event_is_enter(enter));
    typio_key_event_free(enter);

    TypioKeyEvent *esc = typio_key_event_new(TYPIO_EVENT_KEY_PRESS, 0, TYPIO_KEY_Escape, 0);
    ASSERT(typio_key_event_is_escape(esc));
    typio_key_event_free(esc);

    TypioKeyEvent *space = typio_key_event_new(TYPIO_EVENT_KEY_PRESS, 0, TYPIO_KEY_space, 0);
    ASSERT(typio_key_event_is_space(space));
    typio_key_event_free(space);

    TypioKeyEvent *left = typio_key_event_new(TYPIO_EVENT_KEY_PRESS, 0, TYPIO_KEY_Left, 0);
    ASSERT(typio_key_event_is_arrow(left));
    typio_key_event_free(left);
}

int main(void) {
    printf("Running core tests:\n");

    run_test_instance_create();
    run_test_instance_config();
    run_test_instance_init();
    run_test_context_create();
    run_test_context_focus();
    run_test_context_properties();
    run_test_basic_engine_commit();
    run_test_key_event();
    run_test_key_event_modifiers();
    run_test_key_event_special();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
