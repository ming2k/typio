/**
 * @file test_config_schema.c
 * @brief Tests for the config schema registry
 */

#include "typio/typio.h"
#include "typio/config_schema.h"
#include "typio/dbus_protocol.h"
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
#define ASSERT_NOT_NULL(a) ASSERT((a) != NULL)
#define ASSERT_NULL(a) ASSERT((a) == NULL)
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)

/* Test: schema lookup */
TEST(schema_find_existing) {
    const TypioConfigField *f = typio_config_schema_find("display.font_size");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(f->type, TYPIO_FIELD_INT);
    ASSERT_EQ(f->def.i, 11);
    ASSERT_NOT_NULL(f->ui_label);
    ASSERT_STR_EQ(f->ui_label, "Font size");
    ASSERT_NULL(f->runtime_property);
}

TEST(schema_find_basic_route_mode) {
    const TypioConfigField *f = typio_config_schema_find("engines.basic.printable_key_mode");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(f->type, TYPIO_FIELD_STRING);
    ASSERT_STR_EQ(f->def.s, "forward");
    ASSERT_STR_EQ(f->ui_label, "Printable keys");
    ASSERT_STR_EQ(f->ui_section, "basic");
    ASSERT_NOT_NULL(f->ui_options);
    ASSERT_STR_EQ(f->ui_options[0], "forward");
    ASSERT_STR_EQ(f->ui_options[1], "commit");
    ASSERT_NULL(f->runtime_property);
}

TEST(schema_find_keyboard_per_app_preferences) {
    const TypioConfigField *f = typio_config_schema_find("keyboard.per_app_preferences");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(f->type, TYPIO_FIELD_BOOL);
    ASSERT(f->def.b);
    ASSERT_STR_EQ(f->ui_label, "Per-app preferences");
    ASSERT_STR_EQ(f->ui_section, "keyboard");
    ASSERT_NULL(f->runtime_property);
}

TEST(schema_find_missing) {
    const TypioConfigField *f = typio_config_schema_find("nonexistent.key");
    ASSERT_NULL(f);
}

TEST(schema_find_rime_full_check_removed) {
    const TypioConfigField *f = typio_config_schema_find("engines.rime.full_check");
    ASSERT_NULL(f);
}

TEST(schema_find_null) {
    const TypioConfigField *f = typio_config_schema_find(NULL);
    ASSERT_NULL(f);
}

TEST(runtime_property_lookup) {
    ASSERT_STR_EQ(typio_config_schema_runtime_property("default_engine"),
                  TYPIO_STATUS_PROP_ACTIVE_KEYBOARD_ENGINE);
    ASSERT_STR_EQ(typio_config_schema_runtime_property("default_voice_engine"),
                  TYPIO_STATUS_PROP_ACTIVE_VOICE_ENGINE);
    ASSERT_NULL(typio_config_schema_runtime_property("engines.rime.font_size"));
    ASSERT_NULL(typio_config_schema_runtime_property(NULL));
}

/* Test: defaults application */
TEST(apply_defaults_empty_config) {
    TypioConfig *config = typio_config_new();
    ASSERT_NOT_NULL(config);

    typio_config_apply_defaults(config);

    /* Check that defaults were applied */
    ASSERT_EQ(typio_config_get_int(config, "display.font_size", 0), 11);
    ASSERT(typio_config_get_bool(config, "notifications.enable", false));
    ASSERT(typio_config_get_bool(config, "notifications.startup_checks", false));
    ASSERT_EQ(typio_config_get_int(config, "notifications.cooldown_ms", 0), 15000);
    ASSERT_STR_EQ(typio_config_get_string(config, "display.popup_theme", ""), "auto");
    ASSERT_STR_EQ(typio_config_get_string(config, "display.candidate_layout", ""), "horizontal");
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.basic.printable_key_mode", ""),
                  "forward");
    ASSERT(typio_config_get_bool(config, "keyboard.per_app_preferences", false));
    ASSERT_STR_EQ(typio_config_get_string(config, "shortcuts.switch_engine", ""), "Ctrl+Shift");
    ASSERT_STR_EQ(typio_config_get_string(config, "shortcuts.emergency_exit", ""), "Ctrl+Shift+Escape");
    ASSERT_STR_EQ(typio_config_get_string(config, "shortcuts.voice_ptt", ""), "Super+v");

    typio_config_free(config);
}

TEST(apply_defaults_preserves_existing) {
    TypioConfig *config = typio_config_new();
    ASSERT_NOT_NULL(config);

    /* Set a non-default value */
    typio_config_set_int(config, "display.font_size", 14);

    typio_config_apply_defaults(config);

    /* Existing values should NOT be overwritten */
    ASSERT_EQ(typio_config_get_int(config, "display.font_size", 0), 14);

    /* Missing values should get defaults */
    ASSERT(typio_config_get_bool(config, "notifications.enable", false));

    typio_config_free(config);
}

TEST(stateful_engine_keys_expose_runtime_property) {
    const TypioConfigField *keyboard = typio_config_schema_find("default_engine");
    const TypioConfigField *voice = typio_config_schema_find("default_voice_engine");

    ASSERT_NOT_NULL(keyboard);
    ASSERT_NOT_NULL(voice);
    ASSERT_STR_EQ(keyboard->runtime_property, TYPIO_STATUS_PROP_ACTIVE_KEYBOARD_ENGINE);
    ASSERT_STR_EQ(voice->runtime_property, TYPIO_STATUS_PROP_ACTIVE_VOICE_ENGINE);
}

/* Test: schema field enumeration */
TEST(schema_fields_enumeration) {
    size_t count = 0;
    const TypioConfigField *fields = typio_config_schema_fields(&count);
    ASSERT_NOT_NULL(fields);
    ASSERT(count > 0);

    /* Every field should have a non-NULL key */
    for (size_t i = 0; i < count; i++) {
        ASSERT_NOT_NULL(fields[i].key);
    }
}

/* Test: full lifecycle (load string → apply defaults) */
TEST(full_lifecycle) {
    const char *config_text =
        "default_voice_engine = \"whisper\"\n"
        "[engines.whisper]\n"
        "model = \"tiny\"\n"
        "language = \"zh\"\n"
        "\n"
        "[engines.rime]\n"
        "shared_data_dir = \"/usr/share/rime-data\"\n";

    TypioConfig *config = typio_config_load_string(config_text);
    ASSERT_NOT_NULL(config);

    typio_config_apply_defaults(config);

    /* Canonical values preserved */
    ASSERT_STR_EQ(typio_config_get_string(config, "default_voice_engine", ""), "whisper");
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.whisper.model", ""), "tiny");
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.whisper.language", ""), "zh");

    /* Defaults applied */
    ASSERT_EQ(typio_config_get_int(config, "display.font_size", 0), 11);
    ASSERT_STR_EQ(typio_config_get_string(config, "display.popup_theme", ""), "auto");

    /* Existing values preserved */
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.rime.shared_data_dir", ""),
                  "/usr/share/rime-data");

    typio_config_free(config);
}

int main(void) {
    printf("Running config schema tests:\n");

    run_test_schema_find_existing();
    run_test_schema_find_basic_route_mode();
    run_test_schema_find_keyboard_per_app_preferences();
    run_test_schema_find_missing();
    run_test_schema_find_rime_full_check_removed();
    run_test_schema_find_null();
    run_test_runtime_property_lookup();
    run_test_apply_defaults_empty_config();
    run_test_apply_defaults_preserves_existing();
    run_test_stateful_engine_keys_expose_runtime_property();
    run_test_schema_fields_enumeration();
    run_test_full_lifecycle();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
