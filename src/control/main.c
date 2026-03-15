/**
 * @file main.c
 * @brief GTK4 control panel for Typio
 */

#include "status/status.h"
#include "typio/typio.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <sys/stat.h>

/* --- Model download infrastructure (shared by whisper.cpp and sherpa-onnx) --- */

typedef struct ModelInfo {
    const char *name;           /* internal identifier */
    const char *display_name;   /* shown in UI */
    const char *size_label;     /* e.g. "142 MB" */
    gint64 expected_size;       /* approximate archive bytes for progress */
    const char *url;            /* full download URL */
    const char *filename;       /* archive filename (NULL = single file, no extract) */
    const char *extract_dir;    /* directory name after extraction (NULL = no extract) */
} ModelInfo;

/* Whisper.cpp (ggml) models — single binary files */
#define WHISPER_MODEL_URL_BASE \
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main"

static const ModelInfo whisper_models[] = {
    { "tiny",   "Tiny",   "75 MB",   75 * 1024 * 1024LL,
      WHISPER_MODEL_URL_BASE "/ggml-tiny.bin", NULL, NULL },
    { "base",   "Base",   "142 MB",  142 * 1024 * 1024LL,
      WHISPER_MODEL_URL_BASE "/ggml-base.bin", NULL, NULL },
    { "small",  "Small",  "466 MB",  466 * 1024 * 1024LL,
      WHISPER_MODEL_URL_BASE "/ggml-small.bin", NULL, NULL },
    { "medium", "Medium", "1.5 GB",  1536 * 1024 * 1024LL,
      WHISPER_MODEL_URL_BASE "/ggml-medium.bin", NULL, NULL },
    { "large",  "Large",  "2.9 GB",  2952 * 1024 * 1024LL,
      WHISPER_MODEL_URL_BASE "/ggml-large.bin", NULL, NULL },
};
#define WHISPER_MODEL_COUNT \
    (sizeof(whisper_models) / sizeof(whisper_models[0]))

/* Sherpa-ONNX models — tar.bz2 archives */
#define SHERPA_MODEL_URL_BASE \
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models"

static const ModelInfo sherpa_models[] = {
    { "sense-voice",
      "SenseVoice (zh/en/ja/ko/yue)", "~230 MB",
      1048 * 1024 * 1024LL,
      SHERPA_MODEL_URL_BASE
          "/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2",
      "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2",
      "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17" },
    { "whisper-tiny",
      "Whisper Tiny (multilingual)", "~110 MB",
      116 * 1024 * 1024LL,
      SHERPA_MODEL_URL_BASE "/sherpa-onnx-whisper-tiny.tar.bz2",
      "sherpa-onnx-whisper-tiny.tar.bz2",
      "sherpa-onnx-whisper-tiny" },
    { "whisper-base",
      "Whisper Base (multilingual)", "~197 MB",
      208 * 1024 * 1024LL,
      SHERPA_MODEL_URL_BASE "/sherpa-onnx-whisper-base.tar.bz2",
      "sherpa-onnx-whisper-base.tar.bz2",
      "sherpa-onnx-whisper-base" },
    { "whisper-small",
      "Whisper Small (multilingual)", "~609 MB",
      639 * 1024 * 1024LL,
      SHERPA_MODEL_URL_BASE "/sherpa-onnx-whisper-small.tar.bz2",
      "sherpa-onnx-whisper-small.tar.bz2",
      "sherpa-onnx-whisper-small" },
};
#define SHERPA_MODEL_COUNT \
    (sizeof(sherpa_models) / sizeof(sherpa_models[0]))

typedef struct ModelRow {
    const ModelInfo *info;
    GtkWidget *row_box;
    GtkLabel *status_label;
    GtkButton *action_button;  /* Download or Delete */
    GtkProgressBar *progress;
    GSubprocess *download_proc;
    guint progress_timer;
    char *installed_path;      /* final file or directory path */
    char *tmp_path;            /* temp file during download */
    char *base_dir;            /* parent directory for this model set */
} ModelRow;


typedef struct TypioControl {
    GtkApplication *app;
    GtkWidget *window;
    GtkLabel *availability_label;
    GtkLabel *engine_label;
    GtkDropDown *engine_dropdown;
    GtkStringList *engine_model;
    /* Display settings */
    GtkDropDown *popup_theme_dropdown;
    GtkStringList *popup_theme_model;
    GtkDropDown *candidate_layout_dropdown;
    GtkStringList *candidate_layout_model;
    GtkSpinButton *font_size_spin;
    /* Engine settings (dynamic per-engine config) */
    GtkStack *engine_config_stack;
    GtkEntry *rime_schema_entry;
    GtkSpinButton *rime_page_size_spin;
    GtkSpinButton *mozc_page_size_spin;
    /* Voice settings */
    GtkDropDown *voice_backend_dropdown;
    GtkStringList *voice_backend_model;
    GtkDropDown *voice_model_dropdown;
    GtkStringList *voice_model_list;
    /* Shortcut settings */
    GtkButton *shortcut_switch_engine_btn;
    GtkButton *shortcut_voice_ptt_btn;
    GtkButton *shortcut_recording_btn;  /* which button is currently recording */
    /* State / controls */
    GtkTextBuffer *state_buffer;
    GtkTextBuffer *config_buffer;
    GtkButton *reload_button;
    GtkButton *refresh_button;
    GtkButton *save_config_button;
    GDBusProxy *proxy;
    guint name_watch_id;
    gboolean updating_ui;

    /* Whisper model management */
    ModelRow whisper_rows[WHISPER_MODEL_COUNT];
    char *whisper_dir;

    /* Sherpa-ONNX model management */
    ModelRow sherpa_rows[SHERPA_MODEL_COUNT];
    char *sherpa_dir;
} TypioControl;

static void control_sync_form_from_buffer(TypioControl *control);
static void control_sync_buffer_from_form(TypioControl *control);

/** Scan installed models and repopulate the voice model dropdown. */
static void control_refresh_voice_models(TypioControl *control) {
    if (!control->voice_model_list || !control->voice_model_dropdown) {
        return;
    }

    guint old_count = (guint)g_list_model_get_n_items(
        G_LIST_MODEL(control->voice_model_list));
    gtk_string_list_splice(control->voice_model_list, 0, old_count, nullptr);

    guint backend = gtk_drop_down_get_selected(control->voice_backend_dropdown);
    const char *backend_name = (backend != GTK_INVALID_LIST_POSITION)
        ? gtk_string_list_get_string(control->voice_backend_model, backend)
        : "whisper";

    const char *scan_dir = nullptr;
    if (g_strcmp0(backend_name, "sherpa-onnx") == 0) {
        scan_dir = control->sherpa_dir;
    } else {
        scan_dir = control->whisper_dir;
    }

    if (!scan_dir) {
        return;
    }

    GDir *dir = g_dir_open(scan_dir, 0, nullptr);
    if (!dir) {
        return;
    }

    const char *entry;
    while ((entry = g_dir_read_name(dir)) != NULL) {
        if (g_strcmp0(backend_name, "sherpa-onnx") == 0) {
            /* Sherpa-ONNX: each subdirectory is a model */
            char *full = g_build_filename(scan_dir, entry, nullptr);
            if (g_file_test(full, G_FILE_TEST_IS_DIR)) {
                gtk_string_list_append(control->voice_model_list, entry);
            }
            g_free(full);
        } else {
            /* Whisper: files matching ggml-*.bin → extract name */
            if (g_str_has_prefix(entry, "ggml-") &&
                g_str_has_suffix(entry, ".bin")) {
                /* "ggml-base.bin" → "base" */
                size_t prefix_len = 5; /* strlen("ggml-") */
                size_t suffix_len = 4; /* strlen(".bin") */
                size_t name_len = strlen(entry) - prefix_len - suffix_len;
                char *name = g_strndup(entry + prefix_len, name_len);
                gtk_string_list_append(control->voice_model_list, name);
                g_free(name);
            }
        }
    }
    g_dir_close(dir);
}

static char *variant_value_to_text(GVariant *value) {
    return g_variant_print(value, TRUE);
}

static void control_set_state_text(TypioControl *control, GVariant *state) {
    GString *text;
    GVariantIter iter;
    const char *key;
    GVariant *value;

    if (!control || !control->state_buffer) {
        return;
    }

    text = g_string_new("");
    if (state && g_variant_is_of_type(state, G_VARIANT_TYPE_VARDICT)) {
        g_variant_iter_init(&iter, state);
        while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
            char *rendered = variant_value_to_text(value);
            g_string_append_printf(text, "%s: %s\n", key, rendered ? rendered : "");
            g_free(rendered);
            g_variant_unref(value);
        }
    } else {
        g_string_assign(text, "No engine state available.\n");
    }

    gtk_text_buffer_set_text(control->state_buffer, text->str, -1);
    g_string_free(text, TRUE);
}

static void control_set_config_text(TypioControl *control, GVariant *config_text) {
    const char *text = "default_engine = \"basic\"\n";

    if (!control || !control->config_buffer) {
        return;
    }

    if (config_text && g_variant_is_of_type(config_text, G_VARIANT_TYPE_STRING)) {
        text = g_variant_get_string(config_text, nullptr);
    }

    gtk_text_buffer_set_text(control->config_buffer, text, -1);
    control_sync_form_from_buffer(control);
}

static guint control_find_model_index(GtkStringList *model, const char *value) {
    guint count;

    if (!model || !value) {
        return GTK_INVALID_LIST_POSITION;
    }

    count = (guint)g_list_model_get_n_items(G_LIST_MODEL(model));
    for (guint i = 0; i < count; ++i) {
        const char *item = gtk_string_list_get_string(model, i);
        if (item && g_strcmp0(item, value) == 0) {
            return i;
        }
    }

    return GTK_INVALID_LIST_POSITION;
}

static void control_sync_form_from_buffer(TypioControl *control) {
    GtkTextIter start;
    GtkTextIter end;
    char *content;
    TypioConfig *config;

    if (!control || !control->config_buffer) {
        return;
    }

    gtk_text_buffer_get_bounds(control->config_buffer, &start, &end);
    content = gtk_text_buffer_get_text(control->config_buffer, &start, &end, FALSE);
    if (!content) {
        return;
    }

    config = typio_config_load_string(content);
    g_free(content);
    if (!config) {
        return;
    }

    control->updating_ui = TRUE;

    /* Display settings */
    if (control->popup_theme_dropdown && control->popup_theme_model) {
        guint idx = control_find_model_index(
            control->popup_theme_model,
            typio_config_get_string(config, "engines.rime.popup_theme", "auto"));
        gtk_drop_down_set_selected(control->popup_theme_dropdown, idx);
    }
    if (control->candidate_layout_dropdown && control->candidate_layout_model) {
        guint idx = control_find_model_index(
            control->candidate_layout_model,
            typio_config_get_string(config, "engines.rime.candidate_layout",
                                    "horizontal"));
        gtk_drop_down_set_selected(control->candidate_layout_dropdown, idx);
    }
    if (control->font_size_spin) {
        gtk_spin_button_set_value(control->font_size_spin,
                                  typio_config_get_int(config,
                                      "engines.rime.font_size", 11));
    }

    /* Engine settings */
    if (control->rime_schema_entry) {
        gtk_editable_set_text(GTK_EDITABLE(control->rime_schema_entry),
                              typio_config_get_string(config, "engines.rime.schema", ""));
    }
    if (control->rime_page_size_spin) {
        gtk_spin_button_set_value(control->rime_page_size_spin,
                                  typio_config_get_int(config, "engines.rime.page_size", 9));
    }
    if (control->mozc_page_size_spin) {
        gtk_spin_button_set_value(control->mozc_page_size_spin,
                                  typio_config_get_int(config, "engines.mozc.page_size", 9));
    }

    /* Voice settings */
    if (control->voice_backend_dropdown && control->voice_backend_model) {
        guint idx = control_find_model_index(
            control->voice_backend_model,
            typio_config_get_string(config, "voice.backend", "whisper"));
        gtk_drop_down_set_selected(control->voice_backend_dropdown, idx);
    }
    control_refresh_voice_models(control);
    if (control->voice_model_dropdown && control->voice_model_list) {
        guint idx = control_find_model_index(
            control->voice_model_list,
            typio_config_get_string(config, "voice.model", ""));
        gtk_drop_down_set_selected(control->voice_model_dropdown, idx);
    }
    /* Shortcut settings */
    if (control->shortcut_switch_engine_btn) {
        gtk_button_set_label(control->shortcut_switch_engine_btn,
                             typio_config_get_string(config, "shortcuts.switch_engine",
                                                     "Ctrl+Shift"));
    }
    if (control->shortcut_voice_ptt_btn) {
        gtk_button_set_label(control->shortcut_voice_ptt_btn,
                             typio_config_get_string(config, "shortcuts.voice_ptt",
                                                     "Super+v"));
    }

    control->updating_ui = FALSE;

    typio_config_free(config);
}

static void control_sync_buffer_from_form(TypioControl *control) {
    GtkTextIter start;
    GtkTextIter end;
    char *content;
    char *rendered;
    TypioConfig *config;
    guint selected;
    const char *model_name;

    if (!control || !control->config_buffer || control->updating_ui) {
        return;
    }

    gtk_text_buffer_get_bounds(control->config_buffer, &start, &end);
    content = gtk_text_buffer_get_text(control->config_buffer, &start, &end, FALSE);
    config = content ? typio_config_load_string(content) : nullptr;
    g_free(content);
    if (!config) {
        config = typio_config_new();
    }
    if (!config) {
        return;
    }

    /* Display settings */
    selected = gtk_drop_down_get_selected(control->popup_theme_dropdown);
    model_name = selected == GTK_INVALID_LIST_POSITION
        ? "auto"
        : gtk_string_list_get_string(control->popup_theme_model, selected);
    typio_config_set_string(config, "engines.rime.popup_theme",
                            model_name ? model_name : "auto");

    selected = gtk_drop_down_get_selected(control->candidate_layout_dropdown);
    model_name = selected == GTK_INVALID_LIST_POSITION
        ? "horizontal"
        : gtk_string_list_get_string(control->candidate_layout_model, selected);
    typio_config_set_string(config, "engines.rime.candidate_layout",
                            model_name ? model_name : "horizontal");

    typio_config_set_int(config, "engines.rime.font_size",
                         gtk_spin_button_get_value_as_int(control->font_size_spin));

    /* Engine settings */
    typio_config_set_string(config, "engines.rime.schema",
                            gtk_editable_get_text(GTK_EDITABLE(control->rime_schema_entry)));
    typio_config_set_int(config, "engines.rime.page_size",
                         gtk_spin_button_get_value_as_int(control->rime_page_size_spin));
    typio_config_set_int(config, "engines.mozc.page_size",
                         gtk_spin_button_get_value_as_int(control->mozc_page_size_spin));

    /* Voice settings */
    selected = gtk_drop_down_get_selected(control->voice_backend_dropdown);
    model_name = selected == GTK_INVALID_LIST_POSITION
        ? "whisper"
        : gtk_string_list_get_string(control->voice_backend_model, selected);
    typio_config_set_string(config, "voice.backend",
                            model_name ? model_name : "whisper");

    selected = gtk_drop_down_get_selected(control->voice_model_dropdown);
    if (selected != GTK_INVALID_LIST_POSITION) {
        const char *voice_model =
            gtk_string_list_get_string(control->voice_model_list, selected);
        if (voice_model && *voice_model) {
            typio_config_set_string(config, "voice.model", voice_model);
        }
    }

    /* Shortcut settings */
    if (control->shortcut_switch_engine_btn) {
        const char *val = gtk_button_get_label(control->shortcut_switch_engine_btn);
        if (val && *val)
            typio_config_set_string(config, "shortcuts.switch_engine", val);
    }
    if (control->shortcut_voice_ptt_btn) {
        const char *val = gtk_button_get_label(control->shortcut_voice_ptt_btn);
        if (val && *val)
            typio_config_set_string(config, "shortcuts.voice_ptt", val);
    }

    rendered = typio_config_to_string(config);
    typio_config_free(config);
    if (!rendered) {
        return;
    }

    control->updating_ui = TRUE;
    gtk_text_buffer_set_text(control->config_buffer, rendered, -1);
    control->updating_ui = FALSE;
    free(rendered);
}

static void control_set_engine_model(TypioControl *control,
                                     GVariant *engines,
                                     const char *active_engine) {
    guint count = 0;

    if (!control || !control->engine_model) {
        return;
    }

    control->updating_ui = TRUE;
    count = (guint)g_list_model_get_n_items(G_LIST_MODEL(control->engine_model));
    gtk_string_list_splice(control->engine_model, 0, count, nullptr);

    if (engines && g_variant_is_of_type(engines, G_VARIANT_TYPE("as"))) {
        GVariantIter iter;
        const char *name;
        guint index = 0;
        guint selected = GTK_INVALID_LIST_POSITION;

        g_variant_iter_init(&iter, engines);
        while (g_variant_iter_next(&iter, "&s", &name)) {
            gtk_string_list_append(control->engine_model, name);
            if (active_engine && g_strcmp0(name, active_engine) == 0) {
                selected = index;
            }
            index++;
        }

        gtk_drop_down_set_selected(control->engine_dropdown, selected);
    } else {
        gtk_drop_down_set_selected(control->engine_dropdown, GTK_INVALID_LIST_POSITION);
    }

    control->updating_ui = FALSE;
}

static void control_refresh_from_proxy(TypioControl *control) {
    GVariant *active_engine;
    GVariant *available_engines;
    GVariant *engine_state;
    GVariant *config_text;
    const char *active_name = "";

    if (!control) {
        return;
    }

    if (!control->proxy || !g_dbus_proxy_get_name_owner(control->proxy)) {
        gtk_label_set_text(control->availability_label, "Typio service unavailable");
        gtk_label_set_text(control->engine_label, "Current engine: unavailable");
        control_set_engine_model(control, nullptr, nullptr);
        control_set_state_text(control, nullptr);
        control_set_config_text(control, nullptr);
        gtk_widget_set_sensitive(GTK_WIDGET(control->engine_dropdown), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(control->reload_button), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(control->refresh_button), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(control->save_config_button), FALSE);
        return;
    }

    gtk_label_set_text(control->availability_label, "Connected to Typio");
    gtk_widget_set_sensitive(GTK_WIDGET(control->engine_dropdown), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(control->reload_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(control->refresh_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(control->save_config_button), TRUE);

    active_engine = g_dbus_proxy_get_cached_property(control->proxy, "ActiveEngine");
    available_engines = g_dbus_proxy_get_cached_property(control->proxy, "AvailableEngines");
    engine_state = g_dbus_proxy_get_cached_property(control->proxy, "ActiveEngineState");
    config_text = g_dbus_proxy_get_cached_property(control->proxy, "ConfigText");

    if (active_engine) {
        active_name = g_variant_get_string(active_engine, nullptr);
    }

    {
        char *line = g_strdup_printf("Current engine: %s",
                                     active_name && *active_name ? active_name : "none");
        gtk_label_set_text(control->engine_label, line);
        g_free(line);
    }

    control_set_engine_model(control, available_engines, active_name);

    /* Switch config panel to match active engine */
    if (control->engine_config_stack && active_name && *active_name) {
        if (gtk_stack_get_child_by_name(control->engine_config_stack, active_name)) {
            gtk_stack_set_visible_child_name(control->engine_config_stack, active_name);
        } else {
            gtk_stack_set_visible_child_name(control->engine_config_stack, "empty");
        }
    }

    control_set_state_text(control, engine_state);
    control_set_config_text(control, config_text);

    if (engine_state) {
        g_variant_unref(engine_state);
    }
    if (config_text) {
        g_variant_unref(config_text);
    }
    if (available_engines) {
        g_variant_unref(available_engines);
    }
    if (active_engine) {
        g_variant_unref(active_engine);
    }
}

static void control_call_noarg_method(TypioControl *control, const char *method) {
    GError *error = nullptr;
    GVariant *reply;

    if (!control || !control->proxy) {
        return;
    }

    reply = g_dbus_proxy_call_sync(control->proxy,
                                   method,
                                   nullptr,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   nullptr,
                                   &error);
    if (!reply) {
        gtk_label_set_text(control->availability_label,
                           error ? error->message : "D-Bus call failed");
        g_clear_error(&error);
        return;
    }

    g_variant_unref(reply);
}

static void control_activate_engine(TypioControl *control, const char *engine_name) {
    GError *error = nullptr;
    GVariant *reply;

    if (!control || !control->proxy || !engine_name || !*engine_name) {
        return;
    }

    reply = g_dbus_proxy_call_sync(control->proxy,
                                   "ActivateEngine",
                                   g_variant_new("(s)", engine_name),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   nullptr,
                                   &error);
    if (!reply) {
        gtk_label_set_text(control->availability_label,
                           error ? error->message : "Engine switch failed");
        g_clear_error(&error);
        return;
    }

    g_variant_unref(reply);
}

static void on_reload_clicked([[maybe_unused]] GtkButton *button, gpointer user_data) {
    control_call_noarg_method((TypioControl *)user_data, "ReloadConfig");
}

static void on_refresh_clicked([[maybe_unused]] GtkButton *button, gpointer user_data) {
    control_refresh_from_proxy((TypioControl *)user_data);
}

static void on_save_config_clicked([[maybe_unused]] GtkButton *button, gpointer user_data) {
    TypioControl *control = user_data;
    GtkTextIter start;
    GtkTextIter end;
    char *content;
    GError *error = nullptr;
    GVariant *reply;

    if (!control || !control->proxy || !control->config_buffer) {
        return;
    }

    gtk_text_buffer_get_bounds(control->config_buffer, &start, &end);
    content = gtk_text_buffer_get_text(control->config_buffer, &start, &end, FALSE);
    if (!content) {
        return;
    }

    reply = g_dbus_proxy_call_sync(control->proxy,
                                   "SetConfigText",
                                   g_variant_new("(s)", content),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   nullptr,
                                   &error);
    g_free(content);

    if (!reply) {
        gtk_label_set_text(control->availability_label,
                           error ? error->message : "Failed to save configuration");
        g_clear_error(&error);
        return;
    }

    g_variant_unref(reply);
    control_refresh_from_proxy(control);
}

/** Sync form → buffer → D-Bus save (for real-time settings). */
static void control_sync_and_save(TypioControl *control) {
    GtkTextIter start;
    GtkTextIter end;
    char *content;
    GError *error = nullptr;
    GVariant *reply;

    control_sync_buffer_from_form(control);

    if (!control->proxy || !control->config_buffer) {
        return;
    }

    gtk_text_buffer_get_bounds(control->config_buffer, &start, &end);
    content = gtk_text_buffer_get_text(control->config_buffer, &start, &end, FALSE);
    if (!content) {
        return;
    }

    reply = g_dbus_proxy_call_sync(control->proxy,
                                   "SetConfigText",
                                   g_variant_new("(s)", content),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1, nullptr, &error);
    g_free(content);
    g_clear_error(&error);
    if (reply) {
        g_variant_unref(reply);
    }
}

static void on_form_entry_changed([[maybe_unused]] GtkEditable *editable, gpointer user_data) {
    control_sync_buffer_from_form((TypioControl *)user_data);
}

static void on_form_spin_changed([[maybe_unused]] GtkSpinButton *spin, gpointer user_data) {
    control_sync_buffer_from_form((TypioControl *)user_data);
}

static void on_voice_backend_changed(GObject *object,
                                     [[maybe_unused]] GParamSpec *pspec,
                                     gpointer user_data) {
    TypioControl *control = user_data;
    if (!control->updating_ui && GTK_DROP_DOWN(object)) {
        control_refresh_voice_models(control);
        control_sync_and_save(control);
    }
}

/* Display-tab callbacks: sync + auto-save for real-time effect */
static void on_display_dropdown_changed(GObject *object,
                                        [[maybe_unused]] GParamSpec *pspec,
                                        gpointer user_data) {
    TypioControl *control = user_data;
    if (!control->updating_ui && GTK_DROP_DOWN(object)) {
        control_sync_and_save(control);
    }
}

static void on_display_spin_changed([[maybe_unused]] GtkSpinButton *spin,
                                    gpointer user_data) {
    TypioControl *control = user_data;
    if (!control->updating_ui) {
        control_sync_and_save(control);
    }
}

static void on_engine_selected(GObject *object,
                               [[maybe_unused]] GParamSpec *pspec,
                               gpointer user_data) {
    TypioControl *control = user_data;
    guint selected;
    const char *engine_name;

    if (!control || control->updating_ui) {
        return;
    }

    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected == GTK_INVALID_LIST_POSITION) {
        return;
    }

    engine_name = gtk_string_list_get_string(control->engine_model, selected);
    control_activate_engine(control, engine_name);

    /* Switch config panel to match selected engine */
    if (control->engine_config_stack && engine_name) {
        if (gtk_stack_get_child_by_name(control->engine_config_stack, engine_name)) {
            gtk_stack_set_visible_child_name(control->engine_config_stack, engine_name);
        } else {
            gtk_stack_set_visible_child_name(control->engine_config_stack, "empty");
        }
    }
}

static void on_proxy_properties_changed([[maybe_unused]] GDBusProxy *proxy,
                                        [[maybe_unused]] GVariant *changed_properties,
                                        [[maybe_unused]] const gchar *const *invalidated_properties,
                                        gpointer user_data) {
    control_refresh_from_proxy((TypioControl *)user_data);
}

static void control_clear_proxy(TypioControl *control) {
    if (!control) {
        return;
    }

    if (control->proxy) {
        g_signal_handlers_disconnect_by_func(control->proxy,
                                             G_CALLBACK(on_proxy_properties_changed),
                                             control);
        g_object_unref(control->proxy);
        control->proxy = nullptr;
    }
}

static void on_name_appeared(GDBusConnection *connection,
                             [[maybe_unused]] const gchar *name,
                             [[maybe_unused]] const gchar *name_owner,
                             gpointer user_data) {
    TypioControl *control = user_data;
    GError *error = nullptr;

    control_clear_proxy(control);
    control->proxy = g_dbus_proxy_new_sync(connection,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           nullptr,
                                           TYPIO_STATUS_DBUS_SERVICE,
                                           TYPIO_STATUS_DBUS_PATH,
                                           TYPIO_STATUS_DBUS_INTERFACE,
                                           nullptr,
                                           &error);
    if (!control->proxy) {
        gtk_label_set_text(control->availability_label,
                           error ? error->message : "Failed to create Typio proxy");
        g_clear_error(&error);
        control_refresh_from_proxy(control);
        return;
    }

    g_signal_connect(control->proxy,
                     "g-properties-changed",
                     G_CALLBACK(on_proxy_properties_changed),
                     control);
    control_refresh_from_proxy(control);
}

static void on_name_vanished([[maybe_unused]] GDBusConnection *connection,
                             [[maybe_unused]] const gchar *name,
                             gpointer user_data) {
    control_clear_proxy((TypioControl *)user_data);
    control_refresh_from_proxy((TypioControl *)user_data);
}

/* --- Generic model management (shared by whisper.cpp and sherpa-onnx) --- */

static gboolean model_installed(const ModelRow *row) {
    return g_file_test(row->installed_path,
                       G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR) ||
           g_file_test(row->installed_path, G_FILE_TEST_EXISTS);
}

static void model_update_row_state(ModelRow *row) {
    if (!row->status_label || !row->action_button) {
        return;
    }

    if (row->download_proc) {
        gtk_label_set_text(row->status_label, "Downloading...");
        gtk_button_set_label(row->action_button, "Cancel");
        gtk_widget_set_sensitive(GTK_WIDGET(row->action_button), TRUE);
        gtk_widget_set_visible(GTK_WIDGET(row->progress), TRUE);
        return;
    }

    gtk_widget_set_visible(GTK_WIDGET(row->progress), FALSE);
    gtk_progress_bar_set_fraction(row->progress, 0.0);

    if (model_installed(row)) {
        gtk_label_set_text(row->status_label, "Installed");
        gtk_button_set_label(row->action_button, "Delete");
        gtk_widget_add_css_class(GTK_WIDGET(row->status_label), "success");
    } else {
        gtk_label_set_text(row->status_label, row->info->size_label);
        gtk_button_set_label(row->action_button, "Download");
        gtk_widget_remove_css_class(GTK_WIDGET(row->status_label), "success");
    }
    gtk_widget_set_sensitive(GTK_WIDGET(row->action_button), TRUE);
}

static gboolean model_progress_tick(gpointer user_data) {
    ModelRow *row = user_data;
    struct stat st;

    if (!row->download_proc || !row->tmp_path) {
        return G_SOURCE_REMOVE;
    }

    if (stat(row->tmp_path, &st) == 0 && row->info->expected_size > 0) {
        double fraction = (double)st.st_size / (double)row->info->expected_size;
        if (fraction > 1.0) {
            fraction = 1.0;
        }
        gtk_progress_bar_set_fraction(row->progress, fraction);

        char *text = g_strdup_printf("%.0f%%", fraction * 100.0);
        gtk_progress_bar_set_text(row->progress, text);
        g_free(text);
    }

    return G_SOURCE_CONTINUE;
}

/**
 * Callback for simple downloads (whisper.cpp single-file models).
 * Renames .part file to final path on success.
 */
static void model_simple_download_finished(GObject *source,
                                            GAsyncResult *result,
                                            gpointer user_data) {
    ModelRow *row = user_data;
    GError *error = nullptr;

    gboolean ok = g_subprocess_wait_check_finish(G_SUBPROCESS(source),
                                                  result, &error);

    if (row->progress_timer) {
        g_source_remove(row->progress_timer);
        row->progress_timer = 0;
    }

    g_clear_object(&row->download_proc);

    if (ok && row->tmp_path && row->installed_path) {
        g_rename(row->tmp_path, row->installed_path);
    } else if (row->tmp_path) {
        g_unlink(row->tmp_path);
    }

    g_clear_error(&error);
    g_free(row->tmp_path);
    row->tmp_path = nullptr;

    model_update_row_state(row);
}

/**
 * Callback for archive downloads (sherpa-onnx tar.bz2 models).
 * Extracts the archive into the base directory, then removes it.
 */
static void model_archive_download_finished(GObject *source,
                                              GAsyncResult *result,
                                              gpointer user_data) {
    ModelRow *row = user_data;
    GError *error = nullptr;

    gboolean ok = g_subprocess_wait_check_finish(G_SUBPROCESS(source),
                                                  result, &error);

    if (row->progress_timer) {
        g_source_remove(row->progress_timer);
        row->progress_timer = 0;
    }

    g_clear_object(&row->download_proc);
    g_clear_error(&error);

    if (ok && row->tmp_path && row->base_dir) {
        /* Extract archive into base_dir */
        GError *ext_error = nullptr;
        GSubprocess *extract = g_subprocess_new(
            G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
            &ext_error,
            "tar", "xjf", row->tmp_path, "-C", row->base_dir, nullptr);

        if (extract) {
            /* Synchronous extract — archives are fast to unpack */
            g_subprocess_wait_check(extract, nullptr, &ext_error);
            g_object_unref(extract);
        }
        if (ext_error) {
            gtk_label_set_text(row->status_label, "Extract failed");
            g_clear_error(&ext_error);
        }
        g_unlink(row->tmp_path);
    } else if (row->tmp_path) {
        g_unlink(row->tmp_path);
    }

    g_free(row->tmp_path);
    row->tmp_path = nullptr;

    model_update_row_state(row);
}

static void model_start_download(ModelRow *row) {
    GError *error = nullptr;

    if (row->download_proc) {
        return;
    }

    g_mkdir_with_parents(row->base_dir, 0755);

    gboolean is_archive = (row->info->filename != NULL);

    if (is_archive) {
        /* Download archive to base_dir */
        row->tmp_path = g_build_filename(row->base_dir,
                                          row->info->filename, nullptr);
    } else {
        /* Download single file alongside installed_path */
        char *dir = g_path_get_dirname(row->installed_path);
        char *basename = g_path_get_basename(row->installed_path);
        char *part_name = g_strdup_printf(".%s.part", basename);
        row->tmp_path = g_build_filename(dir, part_name, nullptr);
        g_free(dir);
        g_free(basename);
        g_free(part_name);
    }

    row->download_proc = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        &error,
        "curl", "-fSL", "--connect-timeout", "10",
        "-o", row->tmp_path, row->info->url, nullptr);

    if (!row->download_proc) {
        gtk_label_set_text(row->status_label,
                           error ? error->message : "Failed to start curl");
        g_clear_error(&error);
        g_free(row->tmp_path);
        row->tmp_path = nullptr;
        return;
    }

    model_update_row_state(row);
    row->progress_timer = g_timeout_add(500, model_progress_tick, row);

    g_subprocess_wait_check_async(row->download_proc, nullptr,
                                   is_archive ? model_archive_download_finished
                                              : model_simple_download_finished,
                                   row);
}

static void model_cancel_download(ModelRow *row) {
    if (!row->download_proc) {
        return;
    }
    g_subprocess_force_exit(row->download_proc);
}

/** Recursively remove a directory tree. */
static void remove_directory_recursive(const char *path) {
    GDir *dir = g_dir_open(path, 0, nullptr);
    if (!dir) {
        g_unlink(path);
        return;
    }
    const char *entry;
    while ((entry = g_dir_read_name(dir)) != NULL) {
        char *child = g_build_filename(path, entry, nullptr);
        if (g_file_test(child, G_FILE_TEST_IS_DIR)) {
            remove_directory_recursive(child);
        } else {
            g_unlink(child);
        }
        g_free(child);
    }
    g_dir_close(dir);
    g_rmdir(path);
}

static void on_model_action_clicked([[maybe_unused]] GtkButton *button, gpointer user_data) {
    ModelRow *row = user_data;

    if (row->download_proc) {
        model_cancel_download(row);
        return;
    }

    if (model_installed(row)) {
        if (g_file_test(row->installed_path, G_FILE_TEST_IS_DIR)) {
            remove_directory_recursive(row->installed_path);
        } else {
            g_unlink(row->installed_path);
        }
        model_update_row_state(row);
    } else {
        model_start_download(row);
    }
}

static void model_row_cleanup(ModelRow *row) {
    if (row->download_proc) {
        g_subprocess_force_exit(row->download_proc);
        g_clear_object(&row->download_proc);
    }
    if (row->progress_timer) {
        g_source_remove(row->progress_timer);
        row->progress_timer = 0;
    }
    if (row->tmp_path) {
        g_unlink(row->tmp_path);
        g_free(row->tmp_path);
    }
    g_free(row->installed_path);
}

/* --- Section builders --- */

static GtkWidget *create_model_row_widget(ModelRow *row) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(hbox, 2);
    gtk_widget_set_margin_bottom(hbox, 2);
    row->row_box = hbox;

    GtkWidget *name_label = gtk_label_new(row->info->display_name);
    gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
    gtk_widget_set_hexpand(name_label, TRUE);
    gtk_box_append(GTK_BOX(hbox), name_label);

    row->status_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(row->status_label, 0.0f);
    gtk_widget_set_size_request(GTK_WIDGET(row->status_label), 90, -1);
    gtk_box_append(GTK_BOX(hbox), GTK_WIDGET(row->status_label));

    row->progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_progress_bar_set_show_text(row->progress, TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(row->progress), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(row->progress), FALSE);
    gtk_box_append(GTK_BOX(hbox), GTK_WIDGET(row->progress));

    row->action_button = GTK_BUTTON(gtk_button_new_with_label(""));
    gtk_widget_set_size_request(GTK_WIDGET(row->action_button), 90, -1);
    g_signal_connect(row->action_button, "clicked",
                     G_CALLBACK(on_model_action_clicked), row);
    gtk_box_append(GTK_BOX(hbox), GTK_WIDGET(row->action_button));

    return hbox;
}

static GtkWidget *whisper_create_model_section(TypioControl *control) {
    GtkWidget *frame;
    GtkWidget *vbox;

    frame = gtk_frame_new("Whisper Models (whisper.cpp)");
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);
    gtk_frame_set_child(GTK_FRAME(frame), vbox);

    for (size_t i = 0; i < WHISPER_MODEL_COUNT; i++) {
        ModelRow *row = &control->whisper_rows[i];
        row->info = &whisper_models[i];
        row->base_dir = g_strdup(control->whisper_dir);

        char *filename = g_strdup_printf("ggml-%s.bin", row->info->name);
        row->installed_path = g_build_filename(control->whisper_dir,
                                                filename, nullptr);
        g_free(filename);

        gtk_box_append(GTK_BOX(vbox), create_model_row_widget(row));
        model_update_row_state(row);
    }

    return frame;
}

static GtkWidget *sherpa_create_model_section(TypioControl *control) {
    GtkWidget *frame;
    GtkWidget *vbox;

    frame = gtk_frame_new("Sherpa-ONNX Models");
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);
    gtk_frame_set_child(GTK_FRAME(frame), vbox);

    for (size_t i = 0; i < SHERPA_MODEL_COUNT; i++) {
        ModelRow *row = &control->sherpa_rows[i];
        row->info = &sherpa_models[i];
        row->base_dir = g_strdup(control->sherpa_dir);
        row->installed_path = g_build_filename(control->sherpa_dir,
                                                row->info->extract_dir,
                                                nullptr);

        gtk_box_append(GTK_BOX(vbox), create_model_row_widget(row));
        model_update_row_state(row);
    }

    return frame;
}

static void models_cleanup(TypioControl *control) {
    for (size_t i = 0; i < WHISPER_MODEL_COUNT; i++) {
        model_row_cleanup(&control->whisper_rows[i]);
        g_free(control->whisper_rows[i].base_dir);
    }
    g_free(control->whisper_dir);

    for (size_t i = 0; i < SHERPA_MODEL_COUNT; i++) {
        model_row_cleanup(&control->sherpa_rows[i]);
        g_free(control->sherpa_rows[i].base_dir);
    }
    g_free(control->sherpa_dir);
}

static void on_window_destroy([[maybe_unused]] GtkWidget *widget, gpointer user_data) {
    TypioControl *control = user_data;

    if (!control) {
        return;
    }

    if (control->name_watch_id != 0) {
        g_bus_unwatch_name(control->name_watch_id);
    }
    control_clear_proxy(control);
    models_cleanup(control);
    g_free(control);
}

/* --- Tab page builders --- */

/** Build the per-engine config panel for Rime. */
static GtkWidget *build_rime_config(TypioControl *control) {
    GtkWidget *grid = gtk_grid_new();
    GtkAdjustment *adjustment;

    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Schema"), 0, 0, 1, 1);
    control->rime_schema_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(control->rime_schema_entry), TRUE);
    g_signal_connect(control->rime_schema_entry, "changed",
                     G_CALLBACK(on_form_entry_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->rime_schema_entry), 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Page size"), 0, 1, 1, 1);
    adjustment = gtk_adjustment_new(9.0, 1.0, 20.0, 1.0, 1.0, 0.0);
    control->rime_page_size_spin = GTK_SPIN_BUTTON(
        gtk_spin_button_new(adjustment, 1.0, 0));
    g_signal_connect(control->rime_page_size_spin, "value-changed",
                     G_CALLBACK(on_form_spin_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->rime_page_size_spin), 1, 1, 1, 1);

    return grid;
}

/** Build the per-engine config panel for Mozc. */
static GtkWidget *build_mozc_config(TypioControl *control) {
    GtkWidget *grid = gtk_grid_new();
    GtkAdjustment *adjustment;

    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Page size"), 0, 0, 1, 1);
    adjustment = gtk_adjustment_new(9.0, 1.0, 20.0, 1.0, 1.0, 0.0);
    control->mozc_page_size_spin = GTK_SPIN_BUTTON(
        gtk_spin_button_new(adjustment, 1.0, 0));
    g_signal_connect(control->mozc_page_size_spin, "value-changed",
                     G_CALLBACK(on_form_spin_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->mozc_page_size_spin), 1, 0, 1, 1);

    return grid;
}

static GtkWidget *build_engine_page(TypioControl *control) {
    GtkWidget *page;
    GtkWidget *row;
    GtkWidget *buttons;
    GtkWidget *frame;

    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(page, 16);
    gtk_widget_set_margin_bottom(page, 16);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);

    /* Status */
    control->availability_label = GTK_LABEL(gtk_label_new("Waiting for Typio..."));
    gtk_label_set_xalign(control->availability_label, 0.0f);
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(control->availability_label));

    control->engine_label = GTK_LABEL(gtk_label_new("Current engine:"));
    gtk_label_set_xalign(control->engine_label, 0.0f);
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(control->engine_label));

    /* Engine selector */
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(page), row);

    control->engine_model = gtk_string_list_new(nullptr);
    control->engine_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->engine_model), nullptr));
    gtk_widget_set_hexpand(GTK_WIDGET(control->engine_dropdown), TRUE);
    g_signal_connect(control->engine_dropdown,
                     "notify::selected",
                     G_CALLBACK(on_engine_selected),
                     control);
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(control->engine_dropdown));

    /* Action buttons */
    buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(page), buttons);

    control->reload_button = GTK_BUTTON(gtk_button_new_with_label("Reload Config"));
    g_signal_connect(control->reload_button, "clicked",
                     G_CALLBACK(on_reload_clicked), control);
    gtk_box_append(GTK_BOX(buttons), GTK_WIDGET(control->reload_button));

    control->refresh_button = GTK_BUTTON(gtk_button_new_with_label("Refresh"));
    g_signal_connect(control->refresh_button, "clicked",
                     G_CALLBACK(on_refresh_clicked), control);
    gtk_box_append(GTK_BOX(buttons), GTK_WIDGET(control->refresh_button));

    control->save_config_button = GTK_BUTTON(gtk_button_new_with_label("Save Config"));
    g_signal_connect(control->save_config_button, "clicked",
                     G_CALLBACK(on_save_config_clicked), control);
    gtk_box_append(GTK_BOX(buttons), GTK_WIDGET(control->save_config_button));

    /* Per-engine configuration (switches with selected engine) */
    frame = gtk_frame_new("Engine Configuration");
    gtk_box_append(GTK_BOX(page), frame);

    control->engine_config_stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(control->engine_config_stack,
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    GtkWidget *stack_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(stack_box, 12);
    gtk_widget_set_margin_bottom(stack_box, 12);
    gtk_widget_set_margin_start(stack_box, 12);
    gtk_widget_set_margin_end(stack_box, 12);
    gtk_box_append(GTK_BOX(stack_box), GTK_WIDGET(control->engine_config_stack));
    gtk_frame_set_child(GTK_FRAME(frame), stack_box);

    /* Empty page for engines with no settings (e.g. basic) */
    GtkWidget *empty_label = gtk_label_new("No configuration for this engine.");
    gtk_label_set_xalign(GTK_LABEL(empty_label), 0.0f);
    gtk_stack_add_named(control->engine_config_stack, empty_label, "empty");

    GtkWidget *basic_label = gtk_label_new("No configuration for this engine.");
    gtk_label_set_xalign(GTK_LABEL(basic_label), 0.0f);
    gtk_stack_add_named(control->engine_config_stack, basic_label, "basic");

    gtk_stack_add_named(control->engine_config_stack,
                        build_rime_config(control), "rime");
    gtk_stack_add_named(control->engine_config_stack,
                        build_mozc_config(control), "mozc");

    gtk_stack_set_visible_child_name(control->engine_config_stack, "empty");

    /* Engine state (read-only, fills remaining space) */
    GtkWidget *state_frame = gtk_frame_new("Engine State");
    gtk_widget_set_vexpand(state_frame, TRUE);
    gtk_box_append(GTK_BOX(page), state_frame);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_frame_set_child(GTK_FRAME(state_frame), scroller);

    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
    control->state_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), text_view);

    return page;
}

static GtkWidget *build_voice_page(TypioControl *control) {
    GtkWidget *page;
    GtkWidget *frame;
    GtkWidget *grid;

    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(page, 16);
    gtk_widget_set_margin_bottom(page, 16);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);

    frame = gtk_frame_new("Voice Input");
    gtk_box_append(GTK_BOX(page), frame);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_frame_set_child(GTK_FRAME(frame), grid);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Backend"), 0, 0, 1, 1);
    control->voice_backend_model = gtk_string_list_new(
        (const char *const []){"whisper", "sherpa-onnx", NULL});
    control->voice_backend_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->voice_backend_model), nullptr));
    gtk_widget_set_hexpand(GTK_WIDGET(control->voice_backend_dropdown), TRUE);
    g_signal_connect(control->voice_backend_dropdown, "notify::selected",
                     G_CALLBACK(on_voice_backend_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->voice_backend_dropdown), 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Model"), 0, 1, 1, 1);
    control->voice_model_list = gtk_string_list_new(nullptr);
    control->voice_model_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->voice_model_list), nullptr));
    g_signal_connect(control->voice_model_dropdown, "notify::selected",
                     G_CALLBACK(on_display_dropdown_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->voice_model_dropdown), 1, 1, 1, 1);

    return page;
}

/* --- Shortcut recorder --- */

static const char *gdk_modifier_name(guint mod_bit) {
    if (mod_bit & GDK_CONTROL_MASK) return "Ctrl";
    if (mod_bit & GDK_SHIFT_MASK) return "Shift";
    if (mod_bit & GDK_ALT_MASK) return "Alt";
    if (mod_bit & GDK_SUPER_MASK) return "Super";
    return NULL;
}

/**
 * Build a shortcut string from GDK modifier state and keyval.
 * Returns a newly allocated string like "Ctrl+Shift" or "Super+v".
 */
static char *format_gdk_shortcut(guint keyval, GdkModifierType state) {
    GString *str = g_string_new(NULL);

    /* Append modifiers in canonical order */
    static const guint mod_order[] = {
        GDK_CONTROL_MASK, GDK_ALT_MASK, GDK_SUPER_MASK, GDK_SHIFT_MASK
    };
    for (size_t i = 0; i < G_N_ELEMENTS(mod_order); i++) {
        if (state & mod_order[i]) {
            if (str->len > 0) g_string_append_c(str, '+');
            g_string_append(str, gdk_modifier_name(mod_order[i]));
        }
    }

    /* Is the keyval itself a modifier? If so, omit it (modifier-only chord). */
    gboolean key_is_modifier = (keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R ||
                                keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R ||
                                keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R ||
                                keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R ||
                                keyval == GDK_KEY_Meta_L || keyval == GDK_KEY_Meta_R);

    if (!key_is_modifier && keyval != 0) {
        if (str->len > 0) g_string_append_c(str, '+');
        /* Use gdk_keyval_name for portable naming */
        const char *name = gdk_keyval_name(gdk_keyval_to_lower(keyval));
        if (name)
            g_string_append(str, name);
    }

    return g_string_free(str, FALSE);
}

static void shortcut_btn_clicked([[maybe_unused]] GtkButton *button, gpointer user_data) {
    TypioControl *control = user_data;
    if (control->shortcut_recording_btn == button) {
        /* Cancel recording */
        control->shortcut_recording_btn = NULL;
        control_sync_form_from_buffer(control);  /* restore label */
        return;
    }
    /* Start recording on this button */
    if (control->shortcut_recording_btn) {
        /* Cancel previous recording */
        control_sync_form_from_buffer(control);
    }
    control->shortcut_recording_btn = button;
    gtk_button_set_label(button, "Press shortcut...");
}

static gboolean shortcut_key_pressed([[maybe_unused]] GtkEventControllerKey *ec,
                                     guint keyval,
                                     [[maybe_unused]] guint keycode,
                                     GdkModifierType state,
                                     gpointer user_data) {
    TypioControl *control = user_data;

    if (!control->shortcut_recording_btn)
        return FALSE;

    /* Escape cancels recording */
    if (keyval == GDK_KEY_Escape) {
        control->shortcut_recording_btn = NULL;
        control_sync_form_from_buffer(control);
        return TRUE;
    }

    /* Ignore bare modifier press — wait for either a non-modifier
     * key or a modifier release (handled below). */
    if (keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R ||
        keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R ||
        keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R ||
        keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R ||
        keyval == GDK_KEY_Meta_L || keyval == GDK_KEY_Meta_R)
        return TRUE;  /* consume, wait for more */

    /* Non-modifier key pressed with modifiers → capture combo */
    GdkModifierType relevant = state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK |
                                        GDK_ALT_MASK | GDK_SUPER_MASK);
    if (relevant == 0)
        return TRUE;  /* need at least one modifier */

    char *shortcut = format_gdk_shortcut(keyval, relevant);
    gtk_button_set_label(control->shortcut_recording_btn, shortcut);
    g_free(shortcut);

    control->shortcut_recording_btn = NULL;
    control_sync_and_save(control);
    return TRUE;
}

static void shortcut_key_released([[maybe_unused]] GtkEventControllerKey *ec,
                                  guint keyval,
                                  [[maybe_unused]] guint keycode,
                                  GdkModifierType state,
                                  gpointer user_data) {
    TypioControl *control = user_data;

    if (!control->shortcut_recording_btn)
        return;

    /* Modifier released — check if this completes a modifier-only chord.
     * E.g. user pressed Ctrl, then Shift, then released Shift → "Ctrl+Shift". */
    GdkModifierType relevant = state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK |
                                        GDK_ALT_MASK | GDK_SUPER_MASK);

    /* The released key's modifier is still in 'state' during the release event,
     * so add the releasing key's modifier back to get the full chord. */
    guint released_mod = 0;
    if (keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R)
        released_mod = GDK_CONTROL_MASK;
    else if (keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R)
        released_mod = GDK_SHIFT_MASK;
    else if (keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R)
        released_mod = GDK_ALT_MASK;
    else if (keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R)
        released_mod = GDK_SUPER_MASK;
    else
        return;  /* not a modifier release */

    /* Need at least 2 different modifiers for a modifier-only chord */
    guint full_chord = relevant | released_mod;
    guint mod_count = 0;
    if (full_chord & GDK_CONTROL_MASK) mod_count++;
    if (full_chord & GDK_SHIFT_MASK) mod_count++;
    if (full_chord & GDK_ALT_MASK) mod_count++;
    if (full_chord & GDK_SUPER_MASK) mod_count++;

    if (mod_count < 2)
        return;  /* single modifier alone is not a shortcut */

    char *shortcut = format_gdk_shortcut(0, (GdkModifierType)full_chord);
    gtk_button_set_label(control->shortcut_recording_btn, shortcut);
    g_free(shortcut);

    control->shortcut_recording_btn = NULL;
    control_sync_and_save(control);
}

static GtkWidget *build_shortcuts_page(TypioControl *control) {
    GtkWidget *page;
    GtkWidget *frame;
    GtkWidget *grid;

    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(page, 16);
    gtk_widget_set_margin_bottom(page, 16);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);

    GtkWidget *hint = gtk_label_new(
        "Click a shortcut button, then press the desired key combination.\n"
        "Press Escape to cancel.");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_append(GTK_BOX(page), hint);

    frame = gtk_frame_new("Keyboard Shortcuts");
    gtk_box_append(GTK_BOX(page), frame);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_frame_set_child(GTK_FRAME(frame), grid);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Switch engine"), 0, 0, 1, 1);
    control->shortcut_switch_engine_btn = GTK_BUTTON(
        gtk_button_new_with_label("Ctrl+Shift"));
    gtk_widget_set_hexpand(GTK_WIDGET(control->shortcut_switch_engine_btn), TRUE);
    g_signal_connect(control->shortcut_switch_engine_btn, "clicked",
                     G_CALLBACK(shortcut_btn_clicked), control);
    gtk_grid_attach(GTK_GRID(grid),
                    GTK_WIDGET(control->shortcut_switch_engine_btn), 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Voice (PTT)"), 0, 1, 1, 1);
    control->shortcut_voice_ptt_btn = GTK_BUTTON(
        gtk_button_new_with_label("Super+v"));
    g_signal_connect(control->shortcut_voice_ptt_btn, "clicked",
                     G_CALLBACK(shortcut_btn_clicked), control);
    gtk_grid_attach(GTK_GRID(grid),
                    GTK_WIDGET(control->shortcut_voice_ptt_btn), 1, 1, 1, 1);

    /* Key event controller on the page for capturing shortcuts */
    GtkEventController *key_ec = gtk_event_controller_key_new();
    g_signal_connect(key_ec, "key-pressed",
                     G_CALLBACK(shortcut_key_pressed), control);
    g_signal_connect(key_ec, "key-released",
                     G_CALLBACK(shortcut_key_released), control);
    gtk_widget_add_controller(page, key_ec);

    return page;
}

static GtkWidget *build_display_page(TypioControl *control) {
    GtkWidget *page;
    GtkWidget *frame;
    GtkWidget *grid;
    GtkAdjustment *adjustment;

    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(page, 16);
    gtk_widget_set_margin_bottom(page, 16);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);

    GtkWidget *hint = gtk_label_new("Display settings take effect immediately.");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_box_append(GTK_BOX(page), hint);

    frame = gtk_frame_new("Candidate Popup");
    gtk_box_append(GTK_BOX(page), frame);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_frame_set_child(GTK_FRAME(frame), grid);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Theme"), 0, 0, 1, 1);
    control->popup_theme_model = gtk_string_list_new(
        (const char *const []){"auto", "light", "dark", NULL});
    control->popup_theme_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->popup_theme_model), nullptr));
    gtk_widget_set_hexpand(GTK_WIDGET(control->popup_theme_dropdown), TRUE);
    g_signal_connect(control->popup_theme_dropdown, "notify::selected",
                     G_CALLBACK(on_display_dropdown_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->popup_theme_dropdown), 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Layout"), 0, 1, 1, 1);
    control->candidate_layout_model = gtk_string_list_new(
        (const char *const []){"horizontal", "vertical", NULL});
    control->candidate_layout_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->candidate_layout_model), nullptr));
    g_signal_connect(control->candidate_layout_dropdown, "notify::selected",
                     G_CALLBACK(on_display_dropdown_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->candidate_layout_dropdown), 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Font size"), 0, 2, 1, 1);
    adjustment = gtk_adjustment_new(11.0, 6.0, 72.0, 1.0, 1.0, 0.0);
    control->font_size_spin = GTK_SPIN_BUTTON(
        gtk_spin_button_new(adjustment, 1.0, 0));
    g_signal_connect(control->font_size_spin, "value-changed",
                     G_CALLBACK(on_display_spin_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->font_size_spin), 1, 2, 1, 1);

    return page;
}

static GtkWidget *build_models_page(TypioControl *control) {
    GtkWidget *page;

    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(page, 16);
    gtk_widget_set_margin_bottom(page, 16);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);

    gtk_box_append(GTK_BOX(page), sherpa_create_model_section(control));
    gtk_box_append(GTK_BOX(page), whisper_create_model_section(control));

    return page;
}

static void activate(GtkApplication *app, gpointer user_data) {
    TypioControl *control = user_data;
    GtkWidget *window;
    GtkWidget *notebook;

    control->app = app;

    /* Initialize model directories early (needed by voice model scan) */
    const char *data_home = g_get_user_data_dir();
    control->whisper_dir = g_build_filename(data_home, "typio", "whisper", nullptr);
    control->sherpa_dir = g_build_filename(data_home, "typio", "sherpa-onnx", nullptr);

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Typio Control");
    gtk_window_set_default_size(GTK_WINDOW(window), 580, 480);

    /* Internal config buffer for form ↔ config sync (no UI exposure) */
    control->config_buffer = gtk_text_buffer_new(nullptr);

    notebook = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             build_display_page(control),
                             gtk_label_new("Display"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             build_engine_page(control),
                             gtk_label_new("Engine"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             build_voice_page(control),
                             gtk_label_new("Voice"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             build_shortcuts_page(control),
                             gtk_label_new("Shortcuts"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             build_models_page(control),
                             gtk_label_new("Models"));

    gtk_window_set_child(GTK_WINDOW(window), notebook);
    control->window = window;

    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), control);

    control->name_watch_id = g_bus_watch_name(G_BUS_TYPE_SESSION,
                                              TYPIO_STATUS_DBUS_SERVICE,
                                              G_BUS_NAME_WATCHER_FLAGS_NONE,
                                              on_name_appeared,
                                              on_name_vanished,
                                              control,
                                              nullptr);
    control_refresh_from_proxy(control);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GtkApplication *app;
    TypioControl *control;
    int status;

    control = g_new0(TypioControl, 1);
    app = gtk_application_new("com.hihusky.typio.control",
                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), control);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
