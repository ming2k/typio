/**
 * @file main.c
 * @brief GTK4 control panel for Typio
 */

#include "status/status.h"

#include <gio/gio.h>
#include <gtk/gtk.h>

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
