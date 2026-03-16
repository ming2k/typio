#include "control_internal.h"

#include <assert.h>
#include <errno.h>
#include <glib/gstdio.h>
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

#define ASSERT_NOT_NULL(a) ASSERT((a) != NULL)
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)

static gboolean ensure_dir(const char *path) {
    return g_mkdir_with_parents(path, 0755) == 0;
}

static void write_buffer(GtkTextBuffer *buffer, const char *text) {
    gtk_text_buffer_set_text(buffer, text, -1);
}

static char *selected_model_name(TypioControl *control) {
    guint selected = gtk_drop_down_get_selected(control->voice_model_dropdown);

    if (selected == GTK_INVALID_LIST_POSITION) {
        return NULL;
    }
    return g_strdup(gtk_string_list_get_string(control->voice_model_list, selected));
}

static char *buffer_text(GtkTextBuffer *buffer) {
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void setup_control(TypioControl *control, char *root_template) {
    char whisper_dir[1024];
    char sherpa_dir[1024];

    memset(control, 0, sizeof(*control));
    ASSERT_NOT_NULL(mkdtemp(root_template));
    ASSERT(snprintf(whisper_dir, sizeof(whisper_dir), "%s/whisper", root_template) <
           (int)sizeof(whisper_dir));
    ASSERT(snprintf(sherpa_dir, sizeof(sherpa_dir), "%s/sherpa-onnx", root_template) <
           (int)sizeof(sherpa_dir));
    ASSERT(ensure_dir(whisper_dir));
    ASSERT(ensure_dir(sherpa_dir));

    control->config_buffer = gtk_text_buffer_new(NULL);
    control->window = gtk_window_new();
    control->config_status_label = GTK_LABEL(gtk_label_new(""));
    control->whisper_dir = g_strdup(whisper_dir);
    control->sherpa_dir = g_strdup(sherpa_dir);
    control->config_seeded = FALSE;

    control_build_display_page(control);
    control_build_engines_page(control);
    control_build_shortcuts_page(control);
    gtk_string_list_append(control->voice_backend_model, "whisper");
    gtk_string_list_append(control->voice_backend_model, "sherpa-onnx");
    gtk_drop_down_set_selected(control->voice_backend_dropdown, 0);

    control->config_seeded = TRUE;
}

static void cleanup_control(TypioControl *control) {
    control_models_cleanup(control);
    g_free(control->committed_config_text);
}

TEST(window_builds_actions) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    GtkApplication *app;
    GtkWidget *window;

    setup_control(&control, root);
    app = gtk_application_new("org.typio.ControlTest", G_APPLICATION_NON_UNIQUE);
    ASSERT_NOT_NULL(app);

    window = control_build_window(&control, app);
    ASSERT_NOT_NULL(window);
    ASSERT(control.window == NULL);
    ASSERT_NOT_NULL(control.config_status_label);

    gtk_window_destroy(GTK_WINDOW(window));
    g_object_unref(app);
    cleanup_control(&control);
}

TEST(widget_debug_names_are_stable) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";

    setup_control(&control, root);

    ASSERT_STR_EQ(gtk_widget_get_name(GTK_WIDGET(control.popup_theme_dropdown)),
                  "field-engines-rime-popup-theme");
    ASSERT_STR_EQ(gtk_widget_get_name(GTK_WIDGET(control.notifications_enable_switch)),
                  "field-notifications-enable");
    ASSERT_STR_EQ(gtk_widget_get_name(GTK_WIDGET(control.shortcut_switch_engine_btn)),
                  "shortcut-switch-engine-button");
    ASSERT_STR_EQ(gtk_widget_get_name(GTK_WIDGET(control.rime_schema_dropdown)),
                  "rime-schema-dropdown");

    cleanup_control(&control);
}

TEST(form_changes_update_buffer_immediately) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    char *rendered;

    setup_control(&control, root);
    write_buffer(control.config_buffer,
                 "default_engine = \"basic\"\n"
                 "[engines.rime]\n"
                 "popup_theme = \"auto\"\n"
                 "candidate_layout = \"horizontal\"\n"
                 "font_size = 11\n");
    control_sync_form_from_buffer(&control);

    gtk_spin_button_set_value(control.font_size_spin, 13.0);
    on_display_spin_changed(control.font_size_spin, &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "font_size = 13") != NULL);
    g_free(rendered);

    cleanup_control(&control);
}

TEST(voice_model_selection_follows_staged_config) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    char whisper_model[1024];
    char sherpa_model[1024];
    char *selected = NULL;

    setup_control(&control, root);
    ASSERT(snprintf(whisper_model, sizeof(whisper_model), "%s/ggml-tiny.bin",
                    control.whisper_dir) < (int)sizeof(whisper_model));
    ASSERT(snprintf(sherpa_model, sizeof(sherpa_model), "%s/sense-voice",
                    control.sherpa_dir) < (int)sizeof(sherpa_model));
    {
        FILE *file = fopen(whisper_model, "w");
        ASSERT(file != NULL);
        fclose(file);
    }
    ASSERT(ensure_dir(sherpa_model));

    control.committed_config_text = g_strdup(
        "default_voice_engine = \"whisper\"\n"
        "[engines.whisper]\n"
        "model = \"tiny\"\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    control_sync_form_from_buffer(&control);
    control_refresh_voice_models_from_stage(&control);
    selected = selected_model_name(&control);
    ASSERT(selected);
    ASSERT_STR_EQ(selected, "tiny");
    g_free(selected);

    g_free(control.committed_config_text);
    control.committed_config_text = g_strdup(
        "default_voice_engine = \"sherpa-onnx\"\n"
        "[engines.sherpa-onnx]\n"
        "model = \"sense-voice\"\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    control_sync_form_from_buffer(&control);
    control_refresh_voice_models_from_stage(&control);
    selected = selected_model_name(&control);
    ASSERT(selected);
    ASSERT_STR_EQ(selected, "sense-voice");
    g_free(selected);

    cleanup_control(&control);
}

TEST(voice_backend_sync_writes_default_engine_only) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    char *rendered;

    setup_control(&control, root);
    control.committed_config_text = g_strdup(
        "default_voice_engine = \"whisper\"\n"
        "voice.backend = \"whisper\"\n"
        "voice.model = \"tiny\"\n"
        "[engines.whisper]\n"
        "model = \"tiny\"\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    control_sync_form_from_buffer(&control);

    gtk_drop_down_set_selected(control.voice_backend_dropdown, 1);
    on_voice_backend_changed(G_OBJECT(control.voice_backend_dropdown), NULL, &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "default_voice_engine = \"sherpa-onnx\"") != NULL);
    ASSERT(strstr(rendered, "voice.backend") == NULL);
    ASSERT(strstr(rendered, "voice.model") == NULL);
    g_free(rendered);

    cleanup_control(&control);
}

TEST(notification_settings_roundtrip_through_form) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    char *rendered;

    setup_control(&control, root);
    control.committed_config_text = g_strdup(
        "[notifications]\n"
        "enable = true\n"
        "startup_checks = true\n"
        "runtime = true\n"
        "voice = true\n"
        "cooldown_ms = 15000\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    control_sync_form_from_buffer(&control);

    gtk_switch_set_active(control.notifications_runtime_switch, FALSE);
    gtk_switch_set_active(control.notifications_voice_switch, FALSE);
    gtk_spin_button_set_value(control.notifications_cooldown_spin, 30000.0);
    on_display_switch_changed(G_OBJECT(control.notifications_runtime_switch), NULL, &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "[notifications]") != NULL);
    ASSERT(strstr(rendered, "runtime = false") != NULL);
    ASSERT(strstr(rendered, "voice = false") != NULL);
    ASSERT(strstr(rendered, "cooldown_ms = 30000") != NULL);
    g_free(rendered);

    cleanup_control(&control);
}

TEST(rime_schema_uses_dropdown_selection) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    char *rendered;

    setup_control(&control, root);
    control.committed_config_text = g_strdup(
        "[engines.rime]\n"
        "schema = \"luna_pinyin\"\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    gtk_string_list_append(control.rime_schema_model, "luna_pinyin");
    control_sync_form_from_buffer(&control);

    gtk_drop_down_set_selected(control.rime_schema_dropdown, 0);
    on_display_dropdown_changed(G_OBJECT(control.rime_schema_dropdown), NULL, &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "schema = \"luna_pinyin\"") != NULL);
    g_free(rendered);

    cleanup_control(&control);
}

TEST(unseeded_form_changes_do_not_dirty_buffer) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    char *rendered;

    setup_control(&control, root);
    control.config_seeded = FALSE;
    write_buffer(control.config_buffer, "");

    gtk_spin_button_set_value(control.font_size_spin, 13.0);
    on_display_spin_changed(control.font_size_spin, &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT_STR_EQ(rendered, "");
    g_free(rendered);

    cleanup_control(&control);
}

int main(void) {
    if (!gtk_init_check()) {
        printf("Skipping control config tests: no display available\n");
        return 0;
    }

    printf("Running control config tests:\n");
    run_test_window_builds_actions();
    run_test_widget_debug_names_are_stable();
    run_test_form_changes_update_buffer_immediately();
    run_test_voice_model_selection_follows_staged_config();
    run_test_voice_backend_sync_writes_default_engine_only();
    run_test_notification_settings_roundtrip_through_form();
    run_test_rime_schema_uses_dropdown_selection();
    run_test_unseeded_form_changes_do_not_dirty_buffer();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
