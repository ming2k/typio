#include "control_internal.h"
#include "control_widgets.h"

#include <glib/gstdio.h>
#include <sys/stat.h>

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

const char *control_voice_backend_id(TypioControl *control, guint idx) {
    const char *value;

    if (!control || !control->voice_backend_model ||
        idx == GTK_INVALID_LIST_POSITION) {
        return NULL;
    }

    value = gtk_string_list_get_string(control->voice_backend_model, idx);
    return (value && *value) ? value : NULL;
}

guint control_voice_backend_index(TypioControl *control, const char *id) {
    guint count;

    if (!control || !control->voice_backend_model || !id) {
        return GTK_INVALID_LIST_POSITION;
    }

    count = (guint)g_list_model_get_n_items(G_LIST_MODEL(control->voice_backend_model));
    for (guint i = 0; i < count; i++) {
        const char *value = gtk_string_list_get_string(control->voice_backend_model, i);
        if (value && g_strcmp0(value, id) == 0) {
            return i;
        }
    }

    return GTK_INVALID_LIST_POSITION;
}

gboolean is_voice_backend_name(const char *name) {
    return name &&
           (g_strcmp0(name, "whisper") == 0 ||
            g_strcmp0(name, "sherpa-onnx") == 0);
}

void voice_update_model_sections(TypioControl *control) {
    gboolean is_whisper;
    gboolean is_sherpa;

    if (!control || !control->voice_backend_dropdown) {
        return;
    }

    guint idx = gtk_drop_down_get_selected(control->voice_backend_dropdown);
    const char *id = control_voice_backend_id(control, idx);
    is_whisper = g_strcmp0(id, "whisper") == 0;
    is_sherpa = g_strcmp0(id, "sherpa-onnx") == 0;

    if (control->whisper_models_frame) {
        gtk_widget_set_visible(control->whisper_models_frame, is_whisper);
    }
    if (control->sherpa_models_frame) {
        gtk_widget_set_visible(control->sherpa_models_frame, is_sherpa);
    }
}

void control_refresh_voice_models(TypioControl *control) {
    if (!control->voice_model_list || !control->voice_model_dropdown) {
        return;
    }

    guint old_count = (guint)g_list_model_get_n_items(
        G_LIST_MODEL(control->voice_model_list));
    gtk_string_list_splice(control->voice_model_list, 0, old_count, nullptr);

    guint backend = gtk_drop_down_get_selected(control->voice_backend_dropdown);
    const char *backend_name = control_voice_backend_id(control, backend);
    const char *scan_dir;

    if (!backend_name) {
        return;
    }

    scan_dir = g_strcmp0(backend_name, "sherpa-onnx") == 0
        ? control->sherpa_dir
        : control->whisper_dir;

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
            char *full = g_build_filename(scan_dir, entry, nullptr);
            if (g_file_test(full, G_FILE_TEST_IS_DIR)) {
                gtk_string_list_append(control->voice_model_list, entry);
            }
            g_free(full);
            continue;
        }

        if (g_str_has_prefix(entry, "ggml-") && g_str_has_suffix(entry, ".bin")) {
            size_t prefix_len = 5;
            size_t suffix_len = 4;
            size_t name_len = strlen(entry) - prefix_len - suffix_len;
            char *name = g_strndup(entry + prefix_len, name_len);
            gtk_string_list_append(control->voice_model_list, name);
            g_free(name);
        }
    }

    g_dir_close(dir);
}

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

    if (row->control) {
        control_refresh_voice_models_from_stage(row->control);
    }
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

static void model_simple_download_finished(GObject *source,
                                           GAsyncResult *result,
                                           gpointer user_data) {
    ModelRow *row = user_data;
    GError *error = nullptr;
    gboolean ok = g_subprocess_wait_check_finish(G_SUBPROCESS(source), result, &error);

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

static void model_archive_download_finished(GObject *source,
                                            GAsyncResult *result,
                                            gpointer user_data) {
    ModelRow *row = user_data;
    GError *error = nullptr;
    gboolean ok = g_subprocess_wait_check_finish(G_SUBPROCESS(source), result, &error);

    if (row->progress_timer) {
        g_source_remove(row->progress_timer);
        row->progress_timer = 0;
    }

    g_clear_object(&row->download_proc);
    g_clear_error(&error);

    if (ok && row->tmp_path && row->base_dir) {
        GError *ext_error = nullptr;
        GSubprocess *extract = g_subprocess_new(
            G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
            &ext_error,
            "tar", "xjf", row->tmp_path, "-C", row->base_dir, nullptr);

        if (extract) {
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
    gboolean is_archive;

    if (row->download_proc) {
        return;
    }

    g_mkdir_with_parents(row->base_dir, 0755);
    is_archive = row->info->filename != NULL;

    if (is_archive) {
        row->tmp_path = g_build_filename(row->base_dir, row->info->filename, nullptr);
    } else {
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
    if (row->download_proc) {
        g_subprocess_force_exit(row->download_proc);
    }
}

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
        return;
    }

    model_start_download(row);
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

static GtkWidget *create_model_row_widget(ModelRow *row) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *name_label;
    GtkWidget *meta_label;
    char *row_name = control_build_debug_name("model-row", row->info->name);
    char *text_name = g_strdup_printf("%s-text", row_name);
    char *title_name = g_strdup_printf("%s-title", row_name);
    char *meta_name = g_strdup_printf("%s-meta", row_name);
    char *status_name = g_strdup_printf("%s-status", row_name);
    char *progress_name = g_strdup_printf("%s-progress", row_name);
    char *action_name = g_strdup_printf("%s-action", row_name);

    gtk_widget_add_css_class(hbox, "preference-row");
    row->row_box = hbox;
    control_name_widget(hbox, row_name);

    name_label = gtk_label_new(row->info->display_name);
    gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
    gtk_widget_add_css_class(name_label, "preference-title");
    control_name_widget(name_label, title_name);

    meta_label = gtk_label_new(row->info->size_label);
    gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
    gtk_widget_add_css_class(meta_label, "preference-description");
    gtk_widget_set_hexpand(text_box, TRUE);
    control_name_widget(text_box, text_name);
    control_name_widget(meta_label, meta_name);
    gtk_box_append(GTK_BOX(text_box), name_label);
    gtk_box_append(GTK_BOX(text_box), meta_label);
    gtk_box_append(GTK_BOX(hbox), text_box);

    row->status_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(row->status_label, 0.0f);
    gtk_widget_add_css_class(GTK_WIDGET(row->status_label), "muted-label");
    gtk_widget_set_size_request(GTK_WIDGET(row->status_label), 96, -1);
    control_name_widget(GTK_WIDGET(row->status_label), status_name);
    gtk_box_append(GTK_BOX(hbox), GTK_WIDGET(row->status_label));

    row->progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_progress_bar_set_show_text(row->progress, TRUE);
    gtk_widget_set_size_request(GTK_WIDGET(row->progress), 140, -1);
    gtk_widget_set_visible(GTK_WIDGET(row->progress), FALSE);
    control_name_widget(GTK_WIDGET(row->progress), progress_name);
    gtk_box_append(GTK_BOX(hbox), GTK_WIDGET(row->progress));

    row->action_button = GTK_BUTTON(gtk_button_new_with_label(""));
    gtk_widget_add_css_class(GTK_WIDGET(row->action_button), "control-button");
    gtk_widget_set_size_request(GTK_WIDGET(row->action_button), 110, -1);
    control_name_widget(GTK_WIDGET(row->action_button), action_name);
    g_signal_connect(row->action_button, "clicked",
                     G_CALLBACK(on_model_action_clicked), row);
    gtk_box_append(GTK_BOX(hbox), GTK_WIDGET(row->action_button));

    g_free(row_name);
    g_free(text_name);
    g_free(title_name);
    g_free(meta_name);
    g_free(status_name);
    g_free(progress_name);
    g_free(action_name);
    return hbox;
}

GtkWidget *control_build_whisper_model_section(TypioControl *control) {
    GtkWidget *section = control_create_panel_box_named("whisper-models-section", 10);
    GtkWidget *list = control_create_preferences_list_named("whisper-models-list");

    gtk_box_append(GTK_BOX(section),
                   control_create_section_header_named("whisper-models-header",
                                                       "Whisper models",
                                                       "Local `whisper.cpp` models available for download and removal."));

    for (size_t i = 0; i < WHISPER_MODEL_COUNT; i++) {
        ModelRow *row = &control->whisper_rows[i];
        char *filename;

        row->control = control;
        row->info = &whisper_models[i];
        row->base_dir = g_strdup(control->whisper_dir);
        filename = g_strdup_printf("ggml-%s.bin", row->info->name);
        row->installed_path = g_build_filename(control->whisper_dir, filename, nullptr);
        g_free(filename);

        gtk_list_box_append(GTK_LIST_BOX(list), create_model_row_widget(row));
        model_update_row_state(row);
    }

    gtk_box_append(GTK_BOX(section), list);
    return section;
}

GtkWidget *control_build_sherpa_model_section(TypioControl *control) {
    GtkWidget *section = control_create_panel_box_named("sherpa-models-section", 10);
    GtkWidget *list = control_create_preferences_list_named("sherpa-models-list");

    gtk_box_append(GTK_BOX(section),
                   control_create_section_header_named("sherpa-models-header",
                                                       "Sherpa-ONNX models",
                                                       "Packaged speech models extracted into the local Typio data directory."));

    for (size_t i = 0; i < SHERPA_MODEL_COUNT; i++) {
        ModelRow *row = &control->sherpa_rows[i];

        row->control = control;
        row->info = &sherpa_models[i];
        row->base_dir = g_strdup(control->sherpa_dir);
        row->installed_path = g_build_filename(control->sherpa_dir,
                                               row->info->extract_dir,
                                               nullptr);

        gtk_list_box_append(GTK_LIST_BOX(list), create_model_row_widget(row));
        model_update_row_state(row);
    }

    gtk_box_append(GTK_BOX(section), list);
    return section;
}

void control_models_cleanup(TypioControl *control) {
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
