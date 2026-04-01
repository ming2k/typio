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

static GtkWidget *find_widget_by_name(GtkWidget *root, const char *name) {
    GtkWidget *child;

    if (!root || !name) {
        return NULL;
    }

    if (strcmp(gtk_widget_get_name(root), name) == 0) {
        return root;
    }

    for (child = gtk_widget_get_first_child(root);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *match = find_widget_by_name(child, name);
        if (match) {
            return match;
        }
    }

    return NULL;
}

static guint string_list_count(GtkStringList *model) {
    return model ? (guint)g_list_model_get_n_items(G_LIST_MODEL(model)) : 0;
}

static const char *selected_string_array_value(GPtrArray *values, GtkDropDown *dropdown) {
    guint selected;

    if (!values || !dropdown) {
        return NULL;
    }

    selected = gtk_drop_down_get_selected(dropdown);
    if (selected == GTK_INVALID_LIST_POSITION || selected >= values->len) {
        return NULL;
    }

    return g_ptr_array_index(values, selected);
}

static void assert_string_array_dropdown_selection(GPtrArray *values,
                                                   GtkDropDown *dropdown,
                                                   const char *expected) {
    const char *selected = selected_string_array_value(values, dropdown);

    if (expected) {
        ASSERT(selected != NULL);
        ASSERT_STR_EQ(selected, expected);
    } else {
        ASSERT(selected == NULL);
    }
}

static void assert_string_list_dropdown_selection(GtkStringList *model,
                                                  GtkDropDown *dropdown,
                                                  const char *expected) {
    guint selected;
    const char *value = NULL;

    ASSERT(model != NULL);
    ASSERT(dropdown != NULL);
    selected = gtk_drop_down_get_selected(dropdown);
    if (selected != GTK_INVALID_LIST_POSITION) {
        value = gtk_string_list_get_string(model, selected);
    }

    if (expected) {
        ASSERT(value != NULL);
        ASSERT_STR_EQ(value, expected);
    } else {
        ASSERT(value == NULL);
    }
}

static void assert_runtime_selector_maps_value(ControlStateBinding *binding,
                                               const char *expected) {
    const char *selected;

    ASSERT(binding != NULL);
    control_state_binding_select_value(binding, expected);
    selected = control_state_binding_get_selected_value(binding);

    if (expected) {
        ASSERT(selected != NULL);
        ASSERT_STR_EQ(selected, expected);
    } else {
        ASSERT(selected == NULL);
    }
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
    if (control->engine_id_model) {
        g_ptr_array_unref(control->engine_id_model);
    }
    if (control->engine_order_add_id_model) {
        g_ptr_array_unref(control->engine_order_add_id_model);
    }
    if (control->rime_schema_id_model) {
        g_ptr_array_unref(control->rime_schema_id_model);
    }
    g_free(control->committed_config_text);
    g_free(control->engine_settings_engine);
}

TEST(page_builds_actions) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    GtkWidget *display_page;
    GtkWidget *engines_page;
    GtkWidget *shortcuts_page;

    setup_control(&control, root);
    display_page = control_build_display_page(&control);
    engines_page = control_build_engines_page(&control);
    shortcuts_page = control_build_shortcuts_page(&control);

    ASSERT_NOT_NULL(display_page);
    ASSERT_NOT_NULL(engines_page);
    ASSERT_NOT_NULL(shortcuts_page);
    ASSERT_NOT_NULL(control.config_status_label);
    cleanup_control(&control);
}

TEST(widget_debug_names_are_stable) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";

    setup_control(&control, root);

    ASSERT_STR_EQ(gtk_widget_get_name(GTK_WIDGET(control.popup_theme_dropdown)),
                  "field-display-popup-theme");
    ASSERT_STR_EQ(gtk_widget_get_name(GTK_WIDGET(control.notifications_enable_switch)),
                  "field-notifications-enable");
    ASSERT_STR_EQ(gtk_widget_get_name(GTK_WIDGET(control.shortcut_switch_engine_btn)),
                  "shortcut-switch-engine-button");
    ASSERT_STR_EQ(gtk_widget_get_name(GTK_WIDGET(control.shortcut_emergency_exit_btn)),
                  "shortcut-emergency-exit-button");
    ASSERT_STR_EQ(gtk_widget_get_name(GTK_WIDGET(control.rime_schema_dropdown)),
                  "rime-schema-dropdown");

    cleanup_control(&control);
}

TEST(state_bindings_are_configured) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";

    setup_control(&control, root);

    ASSERT_STR_EQ(control.keyboard_engine_state.config_key, "default_engine");
    ASSERT(control.keyboard_engine_state.source == CONTROL_STATE_VALUE_FROM_RUNTIME);
    ASSERT(control.keyboard_engine_state.dropdown == control.engine_dropdown);
    ASSERT(!gtk_widget_get_visible(GTK_WIDGET(control.engine_dropdown)));

    ASSERT_STR_EQ(control.voice_backend_state.config_key, "default_voice_engine");
    ASSERT(control.voice_backend_state.source == CONTROL_STATE_VALUE_RUNTIME_THEN_CONFIG);
    ASSERT(control.voice_backend_state.dropdown == control.voice_backend_dropdown);

    ASSERT_STR_EQ(control.rime_schema_state.config_key, "engines.rime.schema");
    ASSERT(control.rime_schema_state.source == CONTROL_STATE_VALUE_FROM_RUNTIME);
    ASSERT(control.rime_schema_state.dropdown == control.rime_schema_dropdown);
    ASSERT(control.rime_schema_state.refresh_options != NULL);

    cleanup_control(&control);
}

TEST(engine_settings_use_separate_window) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    GtkWidget *button;

    setup_control(&control, root);

    ASSERT_NOT_NULL(control.engine_settings_window);
    ASSERT(!gtk_widget_get_visible(GTK_WIDGET(control.engine_settings_window)));

    button = gtk_button_new();
    g_object_set_data_full(G_OBJECT(button), "typio-engine-name", g_strdup("rime"), g_free);
    on_engine_settings_edit_clicked(GTK_BUTTON(button), &control);

    ASSERT(gtk_widget_get_visible(GTK_WIDGET(control.engine_settings_window)));
    ASSERT_NOT_NULL(control.engine_settings_engine);
    ASSERT_STR_EQ(control.engine_settings_engine, "rime");
    ASSERT_STR_EQ(gtk_window_get_title(control.engine_settings_window),
                  "Rime settings");

    ASSERT(on_engine_settings_window_close_request(control.engine_settings_window, &control));
    ASSERT(!gtk_widget_get_visible(GTK_WIDGET(control.engine_settings_window)));
    ASSERT(control.engine_settings_engine == NULL);

    cleanup_control(&control);
}

TEST(basic_engine_settings_expose_printable_key_mode) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    GtkWidget *basic_page;
    GtkWidget *field;

    setup_control(&control, root);

    basic_page = gtk_stack_get_child_by_name(control.engine_config_stack, "basic");
    ASSERT_NOT_NULL(basic_page);
    field = find_widget_by_name(basic_page, "field-engines-basic-printable-key-mode");
    ASSERT_NOT_NULL(field);
    ASSERT(GTK_IS_DROP_DOWN(field));

    cleanup_control(&control);
}

TEST(keyboard_page_exposes_per_app_preferences_toggle) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    GtkWidget *engines_page;
    GtkWidget *field;

    setup_control(&control, root);

    engines_page = control_build_engines_page(&control);
    ASSERT_NOT_NULL(engines_page);
    field = find_widget_by_name(engines_page, "field-keyboard-per-app-preferences");
    ASSERT_NOT_NULL(field);
    ASSERT(GTK_IS_SWITCH(field));

    cleanup_control(&control);
}

TEST(keyboard_engine_binding_selects_engine_ids) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";

    setup_control(&control, root);
    g_ptr_array_add(control.engine_id_model, g_strdup("basic"));
    gtk_string_list_append(control.engine_model, "Basic");
    g_ptr_array_add(control.engine_id_model, g_strdup("rime"));
    gtk_string_list_append(control.engine_model, "Rime");

    assert_runtime_selector_maps_value(&control.keyboard_engine_state, "rime");
    assert_string_array_dropdown_selection(control.engine_id_model,
                                           control.engine_dropdown,
                                           "rime");

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

TEST(basic_printable_key_mode_changes_update_buffer_immediately) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    GtkWidget *basic_page;
    GtkDropDown *dropdown;
    char *rendered;

    setup_control(&control, root);
    write_buffer(control.config_buffer,
                 "default_engine = \"basic\"\n"
                 "[engines.basic]\n"
                 "printable_key_mode = \"forward\"\n");
    control_sync_form_from_buffer(&control);

    basic_page = gtk_stack_get_child_by_name(control.engine_config_stack, "basic");
    ASSERT_NOT_NULL(basic_page);
    dropdown = GTK_DROP_DOWN(find_widget_by_name(basic_page,
                                                 "field-engines-basic-printable-key-mode"));
    ASSERT_NOT_NULL(dropdown);

    gtk_drop_down_set_selected(dropdown, 1);
    on_display_dropdown_changed(G_OBJECT(dropdown), NULL, &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "printable_key_mode = \"commit\"") != NULL);
    g_free(rendered);

    cleanup_control(&control);
}

TEST(keyboard_per_app_preferences_changes_update_buffer_immediately) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    GtkWidget *engines_page;
    GtkSwitch *toggle;
    char *rendered;

    setup_control(&control, root);
    write_buffer(control.config_buffer,
                 "default_engine = \"basic\"\n"
                 "[keyboard]\n"
                 "per_app_preferences = true\n");
    control_sync_form_from_buffer(&control);

    engines_page = control_build_engines_page(&control);
    ASSERT_NOT_NULL(engines_page);
    toggle = GTK_SWITCH(find_widget_by_name(engines_page,
                                            "field-keyboard-per-app-preferences"));
    ASSERT_NOT_NULL(toggle);

    gtk_switch_set_active(toggle, FALSE);
    on_display_switch_changed(G_OBJECT(toggle), NULL, &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "per_app_preferences = false") != NULL);
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

TEST(voice_backend_loads_selected_value_from_config) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";

    setup_control(&control, root);
    control.committed_config_text = g_strdup(
        "default_voice_engine = \"sherpa-onnx\"\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    control_sync_form_from_buffer(&control);

    assert_string_list_dropdown_selection(control.voice_backend_model,
                                          control.voice_backend_dropdown,
                                          "sherpa-onnx");

    cleanup_control(&control);
}

TEST(voice_backend_options_come_from_available_engines_not_ordered_engines) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    GVariant *available_engines;
    GVariant *ordered_engines;

    setup_control(&control, root);

    available_engines = g_variant_ref_sink(
        g_variant_new_strv((const char *[]){"basic", "rime", "mozc", "whisper", "sherpa-onnx"}, 5));
    ordered_engines = g_variant_ref_sink(
        g_variant_new_strv((const char *[]){"basic", "rime", "mozc"}, 3));

    control_test_set_engine_model(&control, ordered_engines, NULL);
    control_test_set_voice_backend_model(&control, available_engines);

    ASSERT(string_list_count(control.voice_backend_model) == 2);
    ASSERT_STR_EQ(gtk_string_list_get_string(control.voice_backend_model, 0), "whisper");
    ASSERT_STR_EQ(gtk_string_list_get_string(control.voice_backend_model, 1), "sherpa-onnx");

    g_variant_unref(available_engines);
    g_variant_unref(ordered_engines);
    cleanup_control(&control);
}

TEST(voice_backend_sync_writes_default_engine_only) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    char *rendered;

    setup_control(&control, root);
    control.committed_config_text = g_strdup(
        "default_voice_engine = \"whisper\"\n"
        "[engines.whisper]\n"
        "model = \"tiny\"\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    control_sync_form_from_buffer(&control);

    gtk_drop_down_set_selected(control.voice_backend_dropdown, 1);
    on_voice_backend_changed(G_OBJECT(control.voice_backend_dropdown), NULL, &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "default_voice_engine = \"sherpa-onnx\"") != NULL);
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

TEST(rime_schema_selection_does_not_write_config) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    char *rendered;

    setup_control(&control, root);
    control.committed_config_text = g_strdup("[display]\nfont_size = 11\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    g_ptr_array_add(control.rime_schema_id_model, g_strdup(""));
    gtk_string_list_append(control.rime_schema_model, "Unselected");
    g_ptr_array_add(control.rime_schema_id_model, g_strdup("luna_pinyin"));
    gtk_string_list_append(control.rime_schema_model, "luna_pinyin");
    control_sync_form_from_buffer(&control);

    control_state_binding_select_value(&control.rime_schema_state, "luna_pinyin");
    on_display_dropdown_changed(G_OBJECT(control.rime_schema_dropdown), NULL, &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "schema = \"luna_pinyin\"") == NULL);
    g_free(rendered);

    cleanup_control(&control);
}

TEST(initial_config_seed_replaces_empty_buffer) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    char *rendered;

    setup_control(&control, root);
    write_buffer(control.config_buffer, "");
    control.committed_config_text = NULL;

    control_test_set_config_text(&control,
                                 g_variant_new_string("default_voice_engine = \"whisper\"\n"));

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "default_voice_engine = \"whisper\"") != NULL);
    g_free(rendered);

    cleanup_control(&control);
}

TEST(rime_schema_refresh_options_preserves_unselected_and_configured_value) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    TypioConfig *config;

    setup_control(&control, root);
    config = typio_config_new();
    ASSERT(config != NULL);
    typio_config_set_string(config, "engines.rime.user_data_dir", "/tmp/typio-missing-rime-dir");

    control_refresh_rime_schema_options(&control, config, "m2k_pinyin");

    ASSERT(string_list_count(control.rime_schema_model) == 2);
    ASSERT_STR_EQ(gtk_string_list_get_string(control.rime_schema_model, 0), "Unselected");
    ASSERT_STR_EQ((const char *)g_ptr_array_index(control.rime_schema_id_model, 0), "");
    ASSERT_STR_EQ(gtk_string_list_get_string(control.rime_schema_model, 1), "m2k_pinyin");
    ASSERT_STR_EQ((const char *)g_ptr_array_index(control.rime_schema_id_model, 1), "m2k_pinyin");

    typio_config_free(config);
    cleanup_control(&control);
}

TEST(invalid_voice_backend_selection_does_not_reset_config) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    char *rendered;

    setup_control(&control, root);
    control.committed_config_text = g_strdup(
        "default_voice_engine = \"whisper\"\n"
        "[engines.whisper]\n"
        "model = \"tiny\"\n"
        "[notifications]\n"
        "runtime = true\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    control_sync_form_from_buffer(&control);

    gtk_drop_down_set_selected(control.voice_backend_dropdown, GTK_INVALID_LIST_POSITION);
    gtk_switch_set_active(control.notifications_runtime_switch, FALSE);
    on_display_switch_changed(G_OBJECT(control.notifications_runtime_switch), NULL, &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "default_voice_engine = \"whisper\"") != NULL);
    ASSERT(strstr(rendered, "model = \"tiny\"") != NULL);
    ASSERT(strstr(rendered, "runtime = false") != NULL);
    g_free(rendered);

    cleanup_control(&control);
}

TEST(unknown_dropdown_value_is_not_replaced_by_first_option) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    char *rendered;

    setup_control(&control, root);
    control.committed_config_text = g_strdup(
        "[display]\n"
        "popup_theme = \"sepia\"\n"
        "font_size = 11\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    control_sync_form_from_buffer(&control);

    gtk_spin_button_set_value(control.font_size_spin, 13.0);
    on_display_spin_changed(control.font_size_spin, &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "popup_theme = \"sepia\"") != NULL);
    ASSERT(strstr(rendered, "popup_theme = \"auto\"") == NULL);
    ASSERT(strstr(rendered, "font_size = 13") != NULL);
    g_free(rendered);

    cleanup_control(&control);
}

TEST(programmatic_state_binding_updates_do_not_stage_changes) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    char *rendered;

    setup_control(&control, root);
    control.committed_config_text = g_strdup(
        "default_voice_engine = \"whisper\"\n"
        "[engines.whisper]\n"
        "model = \"tiny\"\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    control_sync_form_from_buffer(&control);

    control_test_apply_state_binding_value(&control,
                                           &control.voice_backend_state,
                                           "default_voice_engine = \"sherpa-onnx\"\n");

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "default_voice_engine = \"whisper\"") != NULL);
    ASSERT(strstr(rendered, "default_voice_engine = \"sherpa-onnx\"") == NULL);
    g_free(rendered);

    cleanup_control(&control);
}

TEST(engine_order_roundtrips_and_preserves_default_engine) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    GtkWidget *button;
    char *rendered;

    setup_control(&control, root);
    g_ptr_array_add(control.engine_id_model, g_strdup("basic"));
    gtk_string_list_append(control.engine_model, "Basic");
    g_ptr_array_add(control.engine_id_model, g_strdup("rime"));
    gtk_string_list_append(control.engine_model, "Rime");
    g_ptr_array_add(control.engine_id_model, g_strdup("mozc"));
    gtk_string_list_append(control.engine_model, "Mozc");
    control_refresh_engine_order_editor(&control);

    control.committed_config_text = g_strdup(
        "default_engine = \"basic\"\n"
        "engine_order = [\"rime\", \"basic\"]\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    control_sync_form_from_buffer(&control);

    ASSERT(string_list_count(control.engine_order_model) == 2);
    ASSERT_STR_EQ(gtk_string_list_get_string(control.engine_order_model, 0), "rime");
    ASSERT_STR_EQ(gtk_string_list_get_string(control.engine_order_model, 1), "basic");
    ASSERT(string_list_count(control.engine_order_add_model) == 1);
    ASSERT_STR_EQ(gtk_string_list_get_string(control.engine_order_add_model, 0), "Mozc");

    gtk_drop_down_set_selected(control.engine_order_add_dropdown, 0);
    on_engine_order_add_clicked(NULL, &control);

    button = gtk_button_new();
    g_object_set_data(G_OBJECT(button), "typio-engine-name", "mozc");
    on_engine_order_move_up_clicked(GTK_BUTTON(button), &control);

    button = gtk_button_new();
    g_object_set_data(G_OBJECT(button), "typio-engine-name", "rime");
    on_engine_order_remove_clicked(GTK_BUTTON(button), &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "default_engine = \"basic\"") != NULL);
    ASSERT(strstr(rendered, "engine_order = [\"mozc\", \"basic\"]") != NULL);
    g_free(rendered);

    cleanup_control(&control);
}

TEST(engine_order_filters_non_keyboard_entries_from_config) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";

    setup_control(&control, root);
    g_ptr_array_add(control.engine_id_model, g_strdup("basic"));
    gtk_string_list_append(control.engine_model, "Basic");
    g_ptr_array_add(control.engine_id_model, g_strdup("rime"));
    gtk_string_list_append(control.engine_model, "Rime");

    control.committed_config_text = g_strdup(
        "default_engine = \"basic\"\n"
        "engine_order = [\"basic\", \"mock-voice\", \"rime\"]\n");
    write_buffer(control.config_buffer, control.committed_config_text);
    control_sync_form_from_buffer(&control);

    ASSERT(string_list_count(control.engine_order_model) == 2);
    ASSERT_STR_EQ(gtk_string_list_get_string(control.engine_order_model, 0), "basic");
    ASSERT_STR_EQ(gtk_string_list_get_string(control.engine_order_model, 1), "rime");

    cleanup_control(&control);
}

TEST(engine_order_mode_actions_toggle_persistence) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";
    GtkWidget *button;
    char *rendered;

    setup_control(&control, root);
    g_ptr_array_add(control.engine_id_model, g_strdup("basic"));
    gtk_string_list_append(control.engine_model, "Basic");
    g_ptr_array_add(control.engine_id_model, g_strdup("rime"));
    gtk_string_list_append(control.engine_model, "Rime");
    g_ptr_array_add(control.engine_id_model, g_strdup("mozc"));
    gtk_string_list_append(control.engine_model, "Mozc");
    write_buffer(control.config_buffer, "default_engine = \"basic\"\n");
    control.committed_config_text = g_strdup("default_engine = \"basic\"\n");
    control_sync_form_from_buffer(&control);
    control_refresh_engine_order_editor(&control);

    ASSERT(strstr(gtk_label_get_text(control.engine_order_mode_label), "Automatic order") != NULL);
    ASSERT(!gtk_widget_is_sensitive(GTK_WIDGET(control.engine_order_reset_button)));
    ASSERT(string_list_count(control.engine_order_add_model) == 0);

    button = gtk_button_new();
    g_object_set_data(G_OBJECT(button), "typio-engine-name", "rime");
    on_engine_order_remove_clicked(GTK_BUTTON(button), &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "engine_order = [\"basic\", \"mozc\"]") != NULL);
    g_free(rendered);
    control_refresh_engine_order_editor(&control);
    ASSERT(strstr(gtk_label_get_text(control.engine_order_mode_label), "Custom order") != NULL);
    ASSERT(gtk_widget_is_sensitive(GTK_WIDGET(control.engine_order_reset_button)));

    on_engine_order_reset_clicked(control.engine_order_reset_button, &control);

    rendered = buffer_text(control.config_buffer);
    ASSERT(rendered != NULL);
    ASSERT(strstr(rendered, "engine_order =") == NULL);
    ASSERT(strstr(rendered, "default_engine = \"basic\"") != NULL);
    g_free(rendered);
    control_refresh_engine_order_editor(&control);
    ASSERT(strstr(gtk_label_get_text(control.engine_order_mode_label), "Automatic order") != NULL);

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

TEST(unseeded_control_has_no_pending_change) {
    TypioControl control;
    char root[] = "/tmp/typio-control-test-XXXXXX";

    setup_control(&control, root);
    control.config_seeded = FALSE;
    write_buffer(control.config_buffer, "");

    ASSERT(!control_has_pending_config_change(&control));

    cleanup_control(&control);
}

int main(void) {
    setbuf(stdout, NULL);
    if (!gtk_init_check()) {
        printf("Skipping control config tests: no display available\n");
        return 0;
    }

    printf("Running control config tests:\n");
    run_test_page_builds_actions();
    run_test_widget_debug_names_are_stable();
    run_test_state_bindings_are_configured();
    run_test_engine_settings_use_separate_window();
    run_test_basic_engine_settings_expose_printable_key_mode();
    run_test_keyboard_page_exposes_per_app_preferences_toggle();
    run_test_keyboard_engine_binding_selects_engine_ids();
    run_test_form_changes_update_buffer_immediately();
    run_test_basic_printable_key_mode_changes_update_buffer_immediately();
    run_test_keyboard_per_app_preferences_changes_update_buffer_immediately();
    run_test_voice_model_selection_follows_staged_config();
    run_test_voice_backend_loads_selected_value_from_config();
    run_test_voice_backend_options_come_from_available_engines_not_ordered_engines();
    run_test_voice_backend_sync_writes_default_engine_only();
    run_test_notification_settings_roundtrip_through_form();
    run_test_rime_schema_selection_does_not_write_config();
    run_test_initial_config_seed_replaces_empty_buffer();
    run_test_rime_schema_refresh_options_preserves_unselected_and_configured_value();
    run_test_invalid_voice_backend_selection_does_not_reset_config();
    run_test_unknown_dropdown_value_is_not_replaced_by_first_option();
    run_test_programmatic_state_binding_updates_do_not_stage_changes();
    run_test_engine_order_roundtrips_and_preserves_default_engine();
    run_test_engine_order_filters_non_keyboard_entries_from_config();
    run_test_engine_order_mode_actions_toggle_persistence();
    run_test_unseeded_form_changes_do_not_dirty_buffer();
    run_test_unseeded_control_has_no_pending_change();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}
