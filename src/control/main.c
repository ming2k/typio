/**
 * @file main.c
 * @brief GTK4 control panel for Typio
 */

#include "status/status.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <sys/stat.h>

/* Whisper model definitions */
#define WHISPER_MODEL_URL_BASE \
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main"

typedef struct WhisperModelInfo {
    const char *name;
    const char *display_name;
    const char *size_label;
    gint64 expected_size;  /* approximate bytes for progress calculation */
} WhisperModelInfo;

static const WhisperModelInfo whisper_models[] = {
    { "tiny",   "Tiny",   "75 MB",   75 * 1024 * 1024LL },
    { "base",   "Base",   "142 MB",  142 * 1024 * 1024LL },
    { "small",  "Small",  "466 MB",  466 * 1024 * 1024LL },
    { "medium", "Medium", "1.5 GB",  1536 * 1024 * 1024LL },
    { "large",  "Large",  "2.9 GB",  2952 * 1024 * 1024LL },
};
#define WHISPER_MODEL_COUNT \
    (sizeof(whisper_models) / sizeof(whisper_models[0]))

typedef struct WhisperModelRow {
    const WhisperModelInfo *info;
    GtkWidget *row_box;
    GtkLabel *status_label;
    GtkButton *action_button;  /* Download or Delete */
    GtkProgressBar *progress;
    GSubprocess *download_proc;
    guint progress_timer;
    char *file_path;
    char *tmp_path;
} WhisperModelRow;

typedef struct TypioControl {
    GtkApplication *app;
    GtkWidget *window;
    GtkLabel *availability_label;
    GtkLabel *engine_label;
    GtkDropDown *engine_dropdown;
    GtkStringList *engine_model;
    GtkTextBuffer *state_buffer;
    GtkButton *reload_button;
    GtkButton *refresh_button;
    GDBusProxy *proxy;
    guint name_watch_id;
    gboolean updating_ui;

    /* Whisper model management */
    WhisperModelRow model_rows[WHISPER_MODEL_COUNT];
    char *whisper_dir;
} TypioControl;

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

static void control_set_engine_model(TypioControl *control,
                                     GVariant *engines,
                                     const char *active_engine) {
    guint count = 0;

    if (!control || !control->engine_model) {
        return;
    }

    control->updating_ui = TRUE;
    count = (guint)g_list_model_get_n_items(G_LIST_MODEL(control->engine_model));
    gtk_string_list_splice(control->engine_model, 0, count, NULL);

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
    const char *active_name = "";

    if (!control) {
        return;
    }

    if (!control->proxy || !g_dbus_proxy_get_name_owner(control->proxy)) {
        gtk_label_set_text(control->availability_label, "Typio service unavailable");
        gtk_label_set_text(control->engine_label, "Current engine: unavailable");
        control_set_engine_model(control, NULL, NULL);
        control_set_state_text(control, NULL);
        gtk_widget_set_sensitive(GTK_WIDGET(control->engine_dropdown), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(control->reload_button), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(control->refresh_button), FALSE);
        return;
    }

    gtk_label_set_text(control->availability_label, "Connected to Typio");
    gtk_widget_set_sensitive(GTK_WIDGET(control->engine_dropdown), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(control->reload_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(control->refresh_button), TRUE);

    active_engine = g_dbus_proxy_get_cached_property(control->proxy, "ActiveEngine");
    available_engines = g_dbus_proxy_get_cached_property(control->proxy, "AvailableEngines");
    engine_state = g_dbus_proxy_get_cached_property(control->proxy, "ActiveEngineState");

    if (active_engine) {
        active_name = g_variant_get_string(active_engine, NULL);
    }

    {
        char *line = g_strdup_printf("Current engine: %s",
                                     active_name && *active_name ? active_name : "none");
        gtk_label_set_text(control->engine_label, line);
        g_free(line);
    }

    control_set_engine_model(control, available_engines, active_name);
    control_set_state_text(control, engine_state);

    if (engine_state) {
        g_variant_unref(engine_state);
    }
    if (available_engines) {
        g_variant_unref(available_engines);
    }
    if (active_engine) {
        g_variant_unref(active_engine);
    }
}

static void control_call_noarg_method(TypioControl *control, const char *method) {
    GError *error = NULL;
    GVariant *reply;

    if (!control || !control->proxy) {
        return;
    }

    reply = g_dbus_proxy_call_sync(control->proxy,
                                   method,
                                   NULL,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
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
    GError *error = NULL;
    GVariant *reply;

    if (!control || !control->proxy || !engine_name || !*engine_name) {
        return;
    }

    reply = g_dbus_proxy_call_sync(control->proxy,
                                   "ActivateEngine",
                                   g_variant_new("(s)", engine_name),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   &error);
    if (!reply) {
        gtk_label_set_text(control->availability_label,
                           error ? error->message : "Engine switch failed");
        g_clear_error(&error);
        return;
    }

    g_variant_unref(reply);
}

static void on_reload_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    control_call_noarg_method((TypioControl *)user_data, "ReloadConfig");
}

static void on_refresh_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    control_refresh_from_proxy((TypioControl *)user_data);
}

static void on_engine_selected(GObject *object,
                               GParamSpec *pspec,
                               gpointer user_data) {
    TypioControl *control = user_data;
    guint selected;
    const char *engine_name;

    (void)pspec;

    if (!control || control->updating_ui) {
        return;
    }

    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected == GTK_INVALID_LIST_POSITION) {
        return;
    }

    engine_name = gtk_string_list_get_string(control->engine_model, selected);
    control_activate_engine(control, engine_name);
}

static void on_proxy_properties_changed(GDBusProxy *proxy,
                                        GVariant *changed_properties,
                                        const gchar *const *invalidated_properties,
                                        gpointer user_data) {
    (void)proxy;
    (void)changed_properties;
    (void)invalidated_properties;
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
        control->proxy = NULL;
    }
}

static void on_name_appeared(GDBusConnection *connection,
                             const gchar *name,
                             const gchar *name_owner,
                             gpointer user_data) {
    TypioControl *control = user_data;
    GError *error = NULL;

    (void)name;
    (void)name_owner;

    control_clear_proxy(control);
    control->proxy = g_dbus_proxy_new_sync(connection,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           NULL,
                                           TYPIO_STATUS_DBUS_SERVICE,
                                           TYPIO_STATUS_DBUS_PATH,
                                           TYPIO_STATUS_DBUS_INTERFACE,
                                           NULL,
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

static void on_name_vanished(GDBusConnection *connection,
                             const gchar *name,
                             gpointer user_data) {
    (void)connection;
    (void)name;
    control_clear_proxy((TypioControl *)user_data);
    control_refresh_from_proxy((TypioControl *)user_data);
}

/* --- Whisper model management --- */

static char *whisper_get_data_dir(void) {
    const char *data_home = g_get_user_data_dir();
    return g_build_filename(data_home, "typio", "whisper", NULL);
}

static char *whisper_model_path(const char *dir, const char *model_name) {
    char *filename = g_strdup_printf("ggml-%s.bin", model_name);
    char *path = g_build_filename(dir, filename, NULL);
    g_free(filename);
    return path;
}

static gboolean whisper_model_exists(const char *path) {
    return g_file_test(path, G_FILE_TEST_EXISTS);
}

static void whisper_update_row_state(WhisperModelRow *row) {
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

    if (whisper_model_exists(row->file_path)) {
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

static gboolean whisper_progress_tick(gpointer user_data) {
    WhisperModelRow *row = user_data;
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

static void whisper_download_finished(GObject *source,
                                       GAsyncResult *result,
                                       gpointer user_data) {
    WhisperModelRow *row = user_data;
    GError *error = NULL;

    gboolean ok = g_subprocess_wait_check_finish(G_SUBPROCESS(source),
                                                  result, &error);

    if (row->progress_timer) {
        g_source_remove(row->progress_timer);
        row->progress_timer = 0;
    }

    g_clear_object(&row->download_proc);

    if (ok && row->tmp_path && row->file_path) {
        /* Move tmp file to final path */
        g_rename(row->tmp_path, row->file_path);
    } else if (row->tmp_path) {
        /* Clean up partial download */
        g_unlink(row->tmp_path);
    }

    g_clear_error(&error);
    g_free(row->tmp_path);
    row->tmp_path = NULL;

    whisper_update_row_state(row);
}

static void whisper_start_download(WhisperModelRow *row, const char *dir) {
    char *url;
    GError *error = NULL;

    if (row->download_proc) {
        return;
    }

    /* Ensure directory exists */
    g_mkdir_with_parents(dir, 0755);

    url = g_strdup_printf("%s/ggml-%s.bin", WHISPER_MODEL_URL_BASE,
                          row->info->name);
    row->tmp_path = g_strdup_printf("%s/.ggml-%s.bin.part",
                                     dir, row->info->name);

    row->download_proc = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        &error,
        "curl", "-fSL", "--connect-timeout", "10",
        "-o", row->tmp_path, url, NULL);

    g_free(url);

    if (!row->download_proc) {
        gtk_label_set_text(row->status_label,
                           error ? error->message : "Failed to start curl");
        g_clear_error(&error);
        g_free(row->tmp_path);
        row->tmp_path = NULL;
        return;
    }

    whisper_update_row_state(row);
    row->progress_timer = g_timeout_add(500, whisper_progress_tick, row);

    g_subprocess_wait_check_async(row->download_proc, NULL,
                                   whisper_download_finished, row);
}

static void whisper_cancel_download(WhisperModelRow *row) {
    if (!row->download_proc) {
        return;
    }

    g_subprocess_force_exit(row->download_proc);
    /* The async callback will handle cleanup */
}

static void on_whisper_action_clicked(GtkButton *button, gpointer user_data) {
    WhisperModelRow *row = user_data;
    (void)button;

    if (row->download_proc) {
        whisper_cancel_download(row);
        return;
    }

    if (whisper_model_exists(row->file_path)) {
        g_unlink(row->file_path);
        whisper_update_row_state(row);
    } else {
        char *dir = g_path_get_dirname(row->file_path);
        whisper_start_download(row, dir);
        g_free(dir);
    }
}

static GtkWidget *whisper_create_model_section(TypioControl *control) {
    GtkWidget *frame;
    GtkWidget *vbox;

    control->whisper_dir = whisper_get_data_dir();

    frame = gtk_frame_new("Whisper Models");
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);
    gtk_frame_set_child(GTK_FRAME(frame), vbox);

    for (size_t i = 0; i < WHISPER_MODEL_COUNT; i++) {
        WhisperModelRow *row = &control->model_rows[i];
        GtkWidget *hbox;

        row->info = &whisper_models[i];
        row->file_path = whisper_model_path(control->whisper_dir,
                                             row->info->name);

        hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_top(hbox, 2);
        gtk_widget_set_margin_bottom(hbox, 2);
        row->row_box = hbox;

        /* Model name */
        GtkWidget *name_label = gtk_label_new(row->info->display_name);
        gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
        gtk_widget_set_size_request(name_label, 70, -1);
        gtk_box_append(GTK_BOX(hbox), name_label);

        /* Status */
        row->status_label = GTK_LABEL(gtk_label_new(""));
        gtk_label_set_xalign(row->status_label, 0.0f);
        gtk_widget_set_size_request(GTK_WIDGET(row->status_label), 90, -1);
        gtk_box_append(GTK_BOX(hbox), GTK_WIDGET(row->status_label));

        /* Progress bar */
        row->progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
        gtk_progress_bar_set_show_text(row->progress, TRUE);
        gtk_widget_set_hexpand(GTK_WIDGET(row->progress), TRUE);
        gtk_widget_set_visible(GTK_WIDGET(row->progress), FALSE);
        gtk_box_append(GTK_BOX(hbox), GTK_WIDGET(row->progress));

        /* Action button */
        row->action_button = GTK_BUTTON(gtk_button_new_with_label(""));
        gtk_widget_set_size_request(GTK_WIDGET(row->action_button), 90, -1);
        g_signal_connect(row->action_button, "clicked",
                         G_CALLBACK(on_whisper_action_clicked), row);
        gtk_box_append(GTK_BOX(hbox), GTK_WIDGET(row->action_button));

        gtk_box_append(GTK_BOX(vbox), hbox);
        whisper_update_row_state(row);
    }

    return frame;
}

static void whisper_cleanup(TypioControl *control) {
    for (size_t i = 0; i < WHISPER_MODEL_COUNT; i++) {
        WhisperModelRow *row = &control->model_rows[i];
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
        g_free(row->file_path);
    }
    g_free(control->whisper_dir);
}

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    TypioControl *control = user_data;

    (void)widget;

    if (!control) {
        return;
    }

    if (control->name_watch_id != 0) {
        g_bus_unwatch_name(control->name_watch_id);
    }
    control_clear_proxy(control);
    whisper_cleanup(control);
    g_free(control);
}

static void activate(GtkApplication *app, gpointer user_data) {
    TypioControl *control = user_data;
    GtkWidget *window;
    GtkWidget *content;
    GtkWidget *row;
    GtkWidget *buttons;
    GtkWidget *scroller;
    GtkWidget *text_view;

    control->app = app;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Typio Control");
    gtk_window_set_default_size(GTK_WINDOW(window), 560, 420);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);
    gtk_widget_set_margin_start(content, 16);
    gtk_widget_set_margin_end(content, 16);

    control->availability_label = GTK_LABEL(gtk_label_new("Waiting for Typio..."));
    gtk_label_set_xalign(control->availability_label, 0.0f);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(control->availability_label));

    control->engine_label = GTK_LABEL(gtk_label_new("Current engine:"));
    gtk_label_set_xalign(control->engine_label, 0.0f);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(control->engine_label));

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(content), row);

    control->engine_model = gtk_string_list_new(NULL);
    control->engine_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->engine_model), NULL));
    gtk_widget_set_hexpand(GTK_WIDGET(control->engine_dropdown), TRUE);
    g_signal_connect(control->engine_dropdown,
                     "notify::selected",
                     G_CALLBACK(on_engine_selected),
                     control);
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(control->engine_dropdown));

    buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(content), buttons);

    control->reload_button = GTK_BUTTON(gtk_button_new_with_label("Reload Config"));
    g_signal_connect(control->reload_button, "clicked",
                     G_CALLBACK(on_reload_clicked), control);
    gtk_box_append(GTK_BOX(buttons), GTK_WIDGET(control->reload_button));

    control->refresh_button = GTK_BUTTON(gtk_button_new_with_label("Refresh"));
    g_signal_connect(control->refresh_button, "clicked",
                     G_CALLBACK(on_refresh_clicked), control);
    gtk_box_append(GTK_BOX(buttons), GTK_WIDGET(control->refresh_button));

    gtk_box_append(GTK_BOX(content), whisper_create_model_section(control));

    scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(content), scroller);

    text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
    control->state_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), text_view);

    gtk_window_set_child(GTK_WINDOW(window), content);
    control->window = window;

    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), control);

    control->name_watch_id = g_bus_watch_name(G_BUS_TYPE_SESSION,
                                              TYPIO_STATUS_DBUS_SERVICE,
                                              G_BUS_NAME_WATCHER_FLAGS_NONE,
                                              on_name_appeared,
                                              on_name_vanished,
                                              control,
                                              NULL);
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
