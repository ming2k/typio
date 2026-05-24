/**
 * @file main.c
 * @brief GTK4 control panel application logic for Typio
 */

#include "control_internal.h"
#include "typio/dbus_protocol.h"

#include <getopt.h>
#include <stdio.h>
#include <string.h>

#ifndef TYPIO_CONTROL_TEST
static gboolean control_verbose_enabled = FALSE;

static gboolean control_env_truthy(const char *value) {
    if (!value || !*value) {
        return FALSE;
    }

    return g_ascii_strcasecmp(value, "1") == 0 ||
           g_ascii_strcasecmp(value, "true") == 0 ||
           g_ascii_strcasecmp(value, "yes") == 0 ||
           g_ascii_strcasecmp(value, "on") == 0;
}

static void control_enable_verbose_logging(void) {
    control_verbose_enabled = TRUE;
    g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
#if GLIB_CHECK_VERSION(2, 72, 0)
    g_log_set_debug_enabled(TRUE);
#endif
}

static void control_print_help(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("  -v, --verbose   Enable verbose control-center logging\n");
    printf("  -h, --help      Show this help message\n");
}
#endif

#ifndef TYPIO_CONTROL_TEST
static void on_proxy_properties_changed([[maybe_unused]] GDBusProxy *proxy,
                                        GVariant *changed_properties,
                                        [[maybe_unused]] const gchar *const *invalidated_properties,
                                        gpointer user_data) {
    char *keys = changed_properties ? g_variant_print(changed_properties, FALSE) : NULL;
    g_debug("on_proxy_properties_changed: %s", keys ? keys : "(null)");
    g_free(keys);
    control_refresh_from_proxy((TypioControl *)user_data);
}

void control_clear_proxy(TypioControl *control) {
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
    g_message("control: status bus name appeared, creating proxy");
    control->proxy = g_dbus_proxy_new_sync(connection,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           nullptr,
                                           TYPIO_STATUS_DBUS_SERVICE,
                                           TYPIO_STATUS_DBUS_PATH,
                                           TYPIO_STATUS_DBUS_INTERFACE,
                                           nullptr,
                                           &error);
    if (!control->proxy) {
        g_warning("on_name_appeared: failed to create proxy: %s",
                  error ? error->message : "unknown error");
        control_update_availability_label(control,
                                          error ? error->message
                                                : "Failed to create Typio proxy",
                                          TRUE);
        g_clear_error(&error);
        control_refresh_from_proxy(control);
        return;
    }

    g_signal_connect(control->proxy,
                     "g-properties-changed",
                     G_CALLBACK(on_proxy_properties_changed),
                     control);
    control_refresh_from_proxy(control);
    if (control->config_seeded && control->committed_config_text &&
        control_has_pending_config_change(control)) {
        control_queue_autosave(control, CONTROL_AUTOSAVE_NORMAL);
    }
}

static void on_name_vanished([[maybe_unused]] GDBusConnection *connection,
                             [[maybe_unused]] const gchar *name,
                             gpointer user_data) {
    g_warning("control: status bus name vanished");
    control_clear_proxy((TypioControl *)user_data);
    control_refresh_from_proxy((TypioControl *)user_data);
}

static void on_window_destroy([[maybe_unused]] GtkWidget *widget, gpointer user_data) {
    TypioControl *control = user_data;

    if (!control) {
        return;
    }

    if (control->name_watch_id != 0) {
        g_bus_unwatch_name(control->name_watch_id);
    }
    if (control->autosave_source_id != 0) {
        g_source_remove(control->autosave_source_id);
    }
    if (control->status_clear_source_id != 0) {
        g_source_remove(control->status_clear_source_id);
    }
    control_clear_proxy(control);
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
    if (control->engine_settings_window) {
        gtk_window_destroy(control->engine_settings_window);
    }
    g_free(control->committed_config_text);
    g_free(control->engine_settings_engine);
    g_free(control);
}

static void activate(GtkApplication *app, gpointer user_data) {
    TypioControl *control = user_data;
    GtkWidget *window;

    control->app = app;
    control->whisper_dir = g_build_filename(g_get_user_data_dir(), "typio", "whisper", nullptr);
    control->sherpa_dir = g_build_filename(g_get_user_data_dir(), "typio", "sherpa-onnx", nullptr);
    control->config_buffer = gtk_text_buffer_new(nullptr);
    window = control_build_window(control, app);
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
    int write_idx = 1;

    if (control_env_truthy(g_getenv("TYPIO_CONTROL_VERBOSE"))) {
        control_enable_verbose_logging();
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            control_enable_verbose_logging();
            continue;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            control_print_help(argv[0]);
            return 0;
        }
        argv[write_idx++] = argv[i];
    }
    argc = write_idx;

    if (control_verbose_enabled) {
        g_message("Typio Control verbose logging enabled");
    }

    control = g_new0(TypioControl, 1);
    app = gtk_application_new("com.hihusky.typio.control",
                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), control);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
#endif
