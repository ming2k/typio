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
    const TypioConfigField *f = typio_config_schema_find("engines.rime.font_size");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(f->type, TYPIO_FIELD_INT);
    ASSERT_EQ(f->def.i, 11);
    ASSERT_NOT_NULL(f->ui_label);
    ASSERT_STR_EQ(f->ui_label, "Font size");
    ASSERT_NULL(f->runtime_property);
}

TEST(schema_find_missing) {
    const TypioConfigField *f = typio_config_schema_find("nonexistent.key");
    ASSERT_NULL(f);
}

TEST(schema_find_null) {
    const TypioConfigField *f = typio_config_schema_find(NULL);
    ASSERT_NULL(f);
}

TEST(runtime_property_lookup) {
    ASSERT_STR_EQ(typio_config_schema_runtime_property("default_engine"),
                  TYPIO_STATUS_PROP_ACTIVE_ENGINE);
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
    ASSERT_EQ(typio_config_get_int(config, "engines.rime.font_size", 0), 11);
    ASSERT_EQ(typio_config_get_int(config, "engines.mozc.page_size", 0), 9);
    ASSERT(typio_config_get_bool(config, "notifications.enable", false));
    ASSERT(typio_config_get_bool(config, "notifications.startup_checks", false));
    ASSERT_EQ(typio_config_get_int(config, "notifications.cooldown_ms", 0), 15000);
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.rime.popup_theme", ""), "auto");
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.rime.candidate_layout", ""), "horizontal");
    ASSERT_STR_EQ(typio_config_get_string(config, "shortcuts.switch_engine", ""), "Ctrl+Shift");
    ASSERT_STR_EQ(typio_config_get_string(config, "shortcuts.voice_ptt", ""), "Super+v");

    typio_config_free(config);
}

TEST(apply_defaults_preserves_existing) {
    TypioConfig *config = typio_config_new();
    ASSERT_NOT_NULL(config);

    /* Set a non-default value */
    typio_config_set_int(config, "engines.rime.font_size", 14);

    typio_config_apply_defaults(config);

    /* Existing values should NOT be overwritten */
    ASSERT_EQ(typio_config_get_int(config, "engines.rime.font_size", 0), 14);

    /* Missing values should get defaults */
    ASSERT(typio_config_get_bool(config, "notifications.enable", false));

    typio_config_free(config);
}

/* Test: legacy migration */
TEST(migrate_legacy_voice_backend) {
    TypioConfig *config = typio_config_new();
    ASSERT_NOT_NULL(config);

    typio_config_set_string(config, "voice.backend", "whisper");

    typio_config_migrate_legacy(config);

    /* Legacy key should be removed */
    ASSERT(!typio_config_has_key(config, "voice.backend"));
    /* Canonical key should have the value */
    ASSERT_STR_EQ(typio_config_get_string(config, "default_voice_engine", ""), "whisper");

    typio_config_free(config);
}

TEST(migrate_legacy_whisper_model) {
    TypioConfig *config = typio_config_new();
    ASSERT_NOT_NULL(config);

    typio_config_set_string(config, "whisper.model", "large");

    typio_config_migrate_legacy(config);

    ASSERT(!typio_config_has_key(config, "whisper.model"));
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.whisper.model", ""), "large");

    typio_config_free(config);
}

TEST(migrate_legacy_canonical_takes_precedence) {
    TypioConfig *config = typio_config_new();
    ASSERT_NOT_NULL(config);

    /* Both canonical and legacy exist */
    typio_config_set_string(config, "default_voice_engine", "sherpa-onnx");
    typio_config_set_string(config, "voice.backend", "whisper");

    typio_config_migrate_legacy(config);

    /* Canonical should win, legacy removed */
    ASSERT_STR_EQ(typio_config_get_string(config, "default_voice_engine", ""), "sherpa-onnx");
    ASSERT(!typio_config_has_key(config, "voice.backend"));

    typio_config_free(config);
}

TEST(migrate_legacy_voice_language) {
    TypioConfig *config = typio_config_new();
    ASSERT_NOT_NULL(config);

    /* whisper.language is unambiguous — always goes to engines.whisper */
    typio_config_set_string(config, "whisper.language", "en");

    typio_config_migrate_legacy(config);

    ASSERT(!typio_config_has_key(config, "whisper.language"));
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.whisper.language", ""), "en");

    typio_config_free(config);
}

TEST(migrate_shared_voice_model_routes_to_sherpa) {
    TypioConfig *config = typio_config_new();
    ASSERT_NOT_NULL(config);

    /* voice.backend = sherpa-onnx  +  voice.model = sensevoice
     * → default_voice_engine = sherpa-onnx, engines.sherpa-onnx.model = sensevoice */
    typio_config_set_string(config, "voice.backend", "sherpa-onnx");
    typio_config_set_string(config, "voice.model", "sensevoice");
    typio_config_set_string(config, "voice.language", "auto");

    typio_config_migrate_legacy(config);

    ASSERT(!typio_config_has_key(config, "voice.backend"));
    ASSERT(!typio_config_has_key(config, "voice.model"));
    ASSERT(!typio_config_has_key(config, "voice.language"));
    ASSERT_STR_EQ(typio_config_get_string(config, "default_voice_engine", ""), "sherpa-onnx");
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.sherpa-onnx.model", ""), "sensevoice");
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.sherpa-onnx.language", ""), "auto");
    /* whisper should NOT get the sherpa model */
    ASSERT(!typio_config_has_key(config, "engines.whisper.model"));

    typio_config_free(config);
}

TEST(migrate_shared_voice_model_routes_to_whisper) {
    TypioConfig *config = typio_config_new();
    ASSERT_NOT_NULL(config);

    /* voice.backend = whisper  +  voice.model = large */
    typio_config_set_string(config, "voice.backend", "whisper");
    typio_config_set_string(config, "voice.model", "large");

    typio_config_migrate_legacy(config);

    ASSERT_STR_EQ(typio_config_get_string(config, "default_voice_engine", ""), "whisper");
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.whisper.model", ""), "large");
    ASSERT(!typio_config_has_key(config, "engines.sherpa-onnx.model"));

    typio_config_free(config);
}

TEST(migrate_shared_voice_model_no_backend_defaults_whisper) {
    TypioConfig *config = typio_config_new();
    ASSERT_NOT_NULL(config);

    /* No voice.backend — voice.model defaults to whisper engine */
    typio_config_set_string(config, "voice.model", "tiny");

    typio_config_migrate_legacy(config);

    ASSERT_STR_EQ(typio_config_get_string(config, "engines.whisper.model", ""), "tiny");
    ASSERT(!typio_config_has_key(config, "voice.model"));

    typio_config_free(config);
}

/* Test: mozc page_size default stays available */
TEST(mozc_page_size_default) {
    const TypioConfigField *mozc = typio_config_schema_find("engines.mozc.page_size");
    ASSERT_NOT_NULL(mozc);
    ASSERT_EQ(mozc->def.i, 9);
}

TEST(stateful_engine_keys_expose_runtime_property) {
    const TypioConfigField *keyboard = typio_config_schema_find("default_engine");
    const TypioConfigField *voice = typio_config_schema_find("default_voice_engine");

    ASSERT_NOT_NULL(keyboard);
    ASSERT_NOT_NULL(voice);
    ASSERT_STR_EQ(keyboard->runtime_property, TYPIO_STATUS_PROP_ACTIVE_ENGINE);
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

/* Test: full lifecycle (load legacy string → migrate → apply defaults) */
TEST(full_lifecycle) {
    const char *legacy_config =
        "[voice]\n"
        "backend = \"whisper\"\n"
        "model = \"tiny\"\n"
        "language = \"zh\"\n"
        "\n"
        "[engines.rime]\n"
        "schema = \"luna_pinyin\"\n";

    TypioConfig *config = typio_config_load_string(legacy_config);
    ASSERT_NOT_NULL(config);

    typio_config_migrate_legacy(config);
    typio_config_apply_defaults(config);

    /* Legacy keys migrated */
    ASSERT_STR_EQ(typio_config_get_string(config, "default_voice_engine", ""), "whisper");
    ASSERT(!typio_config_has_key(config, "voice.backend"));
    /* Shared voice.model/language routed to whisper (matching backend) */
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.whisper.model", ""), "tiny");
    ASSERT(!typio_config_has_key(config, "voice.model"));
    ASSERT(!typio_config_has_key(config, "voice.language"));

    /* Defaults applied */
    ASSERT_EQ(typio_config_get_int(config, "engines.rime.font_size", 0), 11);
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.rime.popup_theme", ""), "auto");

    /* Existing values preserved */
    ASSERT_STR_EQ(typio_config_get_string(config, "engines.rime.schema", ""), "luna_pinyin");

    typio_config_free(config);
}

int main(void) {
    printf("Running config schema tests:\n");

    run_test_schema_find_existing();
    run_test_schema_find_missing();
    run_test_schema_find_null();
    run_test_runtime_property_lookup();
    run_test_apply_defaults_empty_config();
    run_test_apply_defaults_preserves_existing();
    run_test_migrate_legacy_voice_backend();
    run_test_migrate_legacy_whisper_model();
    run_test_migrate_legacy_canonical_takes_precedence();
    run_test_migrate_legacy_voice_language();
    run_test_migrate_shared_voice_model_routes_to_sherpa();
    run_test_migrate_shared_voice_model_routes_to_whisper();
    run_test_migrate_shared_voice_model_no_backend_defaults_whisper();
    run_test_mozc_page_size_default();
    run_test_stateful_engine_keys_expose_runtime_property();
    run_test_schema_fields_enumeration();
    run_test_full_lifecycle();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
