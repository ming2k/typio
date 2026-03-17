#include "control_internal.h"
#include "control_widgets.h"

#include "typio/dbus_protocol.h"
#include "typio/engine_label.h"
#include "typio/rime_schema_list.h"
#include "typio/typio.h"

static char *control_dup_buffer_text(TypioControl *control) {
    GtkTextIter start;
    GtkTextIter end;

    if (!control || !control->config_buffer) {
        return nullptr;
    }

    gtk_text_buffer_get_bounds(control->config_buffer, &start, &end);
    return gtk_text_buffer_get_text(control->config_buffer, &start, &end, FALSE);
}

static char *control_dup_config_string_from_text(const char *content,
                                                 const char *config_key) {
    TypioConfig *config = NULL;
    const char *value = NULL;
    char *result = NULL;

    if (!content || !config_key) {
        return NULL;
    }

    config = typio_config_load_string(content);
    if (!config) {
        return NULL;
    }

    value = typio_config_get_string(config, config_key, NULL);
    if (value && *value) {
        result = g_strdup(value);
    }

    typio_config_free(config);
    return result;
}

static char *control_dup_staged_config_string(TypioControl *control,
                                              const char *config_key,
                                              const char *fallback_text) {
    char *content = NULL;
    char *result = NULL;

    if (control && control->config_buffer) {
        content = control_dup_buffer_text(control);
    } else if (fallback_text) {
        content = g_strdup(fallback_text);
    }

    if (!content) {
        return NULL;
    }

    result = control_dup_config_string_from_text(content, config_key);
    g_free(content);
    return result;
}

static GVariant *control_get_runtime_property_for_config_key(TypioControl *control,
                                                             const char *config_key) {
    const char *property_name;

    if (!control || !control->proxy || !config_key) {
        return NULL;
    }

    property_name = typio_config_schema_runtime_property(config_key);
    if (!property_name) {
        return NULL;
    }

    return g_dbus_proxy_get_cached_property(control->proxy, property_name);
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

static guint control_engine_order_index(TypioControl *control, const char *engine_name);
static void control_materialize_current_engine_order(TypioControl *control);
static void control_queue_engine_order_editor_refresh(TypioControl *control);
static void control_set_config_text(TypioControl *control, GVariant *config_text);
static void control_set_engine_model(TypioControl *control,
                                     GVariant *engines,
                                     GVariant *display_names);
static void control_set_voice_backend_model(TypioControl *control,
                                            GVariant *engines);

static const char *control_lookup_engine_display_name(GVariant *display_names,
                                                      const char *engine_name) {
    GVariantIter iter;
    const char *key;
    const char *value;

    if (!display_names || !engine_name ||
        !g_variant_is_of_type(display_names, G_VARIANT_TYPE("a{ss}"))) {
        return NULL;
    }

    g_variant_iter_init(&iter, display_names);
    while (g_variant_iter_next(&iter, "{&s&s}", &key, &value)) {
        if (g_strcmp0(key, engine_name) == 0) {
            return value;
        }
    }

    return NULL;
}

static guint control_string_list_count(GtkStringList *model) {
    if (!model) {
        return 0;
    }

    return (guint)g_list_model_get_n_items(G_LIST_MODEL(model));
}

static gboolean control_string_list_contains(GtkStringList *model, const char *value) {
    return control_find_model_index(model, value) != GTK_INVALID_LIST_POSITION;
}

static guint control_string_array_count(GPtrArray *values) {
    return values ? values->len : 0;
}

static const char *control_string_array_get(GPtrArray *values, guint index) {
    if (!values || index >= values->len) {
        return NULL;
    }

    return g_ptr_array_index(values, index);
}

static gboolean control_string_array_contains(GPtrArray *values, const char *value) {
    if (!values || !value) {
        return FALSE;
    }

    for (guint i = 0; i < values->len; ++i) {
        const char *item = g_ptr_array_index(values, i);
        if (item && g_strcmp0(item, value) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static void control_clear_string_array(GPtrArray *values) {
    if (!values) {
        return;
    }

    g_ptr_array_set_size(values, 0);
}

static void control_clear_string_list(GtkStringList *model) {
    guint count;

    if (!model) {
        return;
    }

    count = control_string_list_count(model);
    if (count > 0) {
        gtk_string_list_splice(model, 0, count, NULL);
    }
}

static void control_append_unique_string(GtkStringList *model, const char *value) {
    if (!model || !value || !*value || control_string_list_contains(model, value)) {
        return;
    }

    gtk_string_list_append(model, value);
}

static char *control_dup_engine_order_status(TypioControl *control, const char *engine_name) {
    GString *status;
    char *default_engine;
    gboolean is_active = FALSE;
    gboolean is_default = FALSE;
    gboolean is_available = FALSE;

    if (!engine_name || !*engine_name) {
        return g_strdup("");
    }

    status = g_string_new(NULL);
    default_engine = control_dup_staged_config_string(control, "default_engine", NULL);
    is_default = default_engine && g_strcmp0(default_engine, engine_name) == 0;
    is_available = control_string_array_contains(control->engine_id_model, engine_name);

    if (control && control->engine_dropdown && control->engine_id_model) {
        guint selected = gtk_drop_down_get_selected(control->engine_dropdown);
        if (selected != GTK_INVALID_LIST_POSITION) {
            const char *active = control_string_array_get(control->engine_id_model, selected);
            is_active = active && g_strcmp0(active, engine_name) == 0;
        }
    }

    if (is_active) {
        g_string_append(status, "Active");
    }
    if (is_default) {
        if (status->len > 0) {
            g_string_append(status, " · ");
        }
        g_string_append(status, "Default");
    }
    if (!is_available) {
        if (status->len > 0) {
            g_string_append(status, " · ");
        }
        g_string_append(status, "Unavailable");
    }

    g_free(default_engine);
    return g_string_free(status, FALSE);
}

static void control_engine_order_reorder_to(TypioControl *control,
                                            const char *engine_name,
                                            const char *target_name,
                                            gboolean place_after) {
    guint source_index;
    guint target_index;
    guint insert_index;
    char *owned_name;
    const char *items[2] = {NULL, NULL};

    if (!control || !engine_name || !*engine_name || !target_name || !*target_name ||
        !control->engine_order_model) {
        return;
    }

    control_materialize_current_engine_order(control);
    source_index = control_engine_order_index(control, engine_name);
    target_index = control_engine_order_index(control, target_name);
    if (source_index == GTK_INVALID_LIST_POSITION ||
        target_index == GTK_INVALID_LIST_POSITION ||
        source_index == target_index) {
        return;
    }

    owned_name = g_strdup(engine_name);
    gtk_string_list_splice(control->engine_order_model, source_index, 1, NULL);
    if (source_index < target_index) {
        target_index--;
    }

    insert_index = place_after ? target_index + 1 : target_index;
    items[0] = owned_name;
    gtk_string_list_splice(control->engine_order_model, insert_index, 0, items);
    g_free(owned_name);
    control_queue_engine_order_editor_refresh(control);
    control_stage_form_change(control, CONTROL_AUTOSAVE_FAST);
}

static void control_engine_order_clear_drop_classes(GtkWidget *widget) {
    if (!widget) {
        return;
    }

    gtk_widget_remove_css_class(widget, "drop-before");
    gtk_widget_remove_css_class(widget, "drop-after");
}

static void control_engine_order_update_drop_class(GtkWidget *widget, double y) {
    int height;
    gboolean place_after;

    if (!widget) {
        return;
    }

    height = gtk_widget_get_height(widget);
    place_after = y >= (double)height / 2.0;
    if (place_after) {
        gtk_widget_remove_css_class(widget, "drop-before");
        gtk_widget_add_css_class(widget, "drop-after");
    } else {
        gtk_widget_remove_css_class(widget, "drop-after");
        gtk_widget_add_css_class(widget, "drop-before");
    }
}

static GdkContentProvider *control_engine_order_drag_prepare(GtkDragSource *source,
                                                             [[maybe_unused]] double x,
                                                             [[maybe_unused]] double y,
                                                             [[maybe_unused]] gpointer user_data) {
    GtkWidget *widget;
    const char *engine_name;

    widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
    engine_name = widget ? g_object_get_data(G_OBJECT(widget), "typio-engine-name") : NULL;
    if (!engine_name || !*engine_name) {
        return NULL;
    }

    return gdk_content_provider_new_typed(G_TYPE_STRING, engine_name);
}

static void control_engine_order_drag_begin(GtkDragSource *source,
                                            [[maybe_unused]] GdkDrag *drag,
                                            [[maybe_unused]] gpointer user_data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));

    if (widget) {
        gtk_widget_add_css_class(widget, "dragging");
    }
}

static void control_engine_order_drag_end(GtkDragSource *source,
                                          [[maybe_unused]] GdkDrag *drag,
                                          [[maybe_unused]] gboolean delete_data,
                                          [[maybe_unused]] gpointer user_data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));

    if (widget) {
        gtk_widget_remove_css_class(widget, "dragging");
        control_engine_order_clear_drop_classes(widget);
    }
}

static gboolean control_engine_order_drop_cb(GtkDropTarget *target,
                                             const GValue *value,
                                             double x,
                                             double y,
                                             gpointer user_data) {
    TypioControl *control = user_data;
    GtkWidget *widget;
    const char *target_name;
    const char *source_name;
    gboolean place_after;

    if (!G_VALUE_HOLDS(value, G_TYPE_STRING)) {
        return FALSE;
    }

    widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));
    target_name = widget ? g_object_get_data(G_OBJECT(widget), "typio-engine-name") : NULL;
    source_name = g_value_get_string(value);
    if (!control || !target_name || !*target_name || !source_name || !*source_name) {
        return FALSE;
    }

    control_engine_order_clear_drop_classes(widget);
    (void)x;
    place_after = y >= (double)gtk_widget_get_height(widget) / 2.0;
    control_engine_order_reorder_to(control, source_name, target_name, place_after);
    return TRUE;
}

static void control_engine_order_drag_motion_enter(GtkDropControllerMotion *motion,
                                                   double x,
                                                   double y,
                                                   gpointer user_data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(motion));

    (void)x;
    (void)user_data;
    control_engine_order_update_drop_class(widget, y);
}

static void control_engine_order_drag_motion_leave(GtkDropControllerMotion *motion,
                                                   gpointer user_data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(motion));

    (void)user_data;
    control_engine_order_clear_drop_classes(widget);
}

static void control_engine_order_drag_motion(GtkDropControllerMotion *motion,
                                             double x,
                                             double y,
                                             gpointer user_data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(motion));

    (void)x;
    (void)user_data;
    control_engine_order_update_drop_class(widget, y);
}

static GtkWidget *control_build_engine_order_row(TypioControl *control,
                                                 const char *engine_name,
                                                 [[maybe_unused]] guint index,
                                                 [[maybe_unused]] guint total) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *shell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *title = gtk_label_new(typio_engine_label_fallback(engine_name));
    GtkWidget *subtitle;
    GtkWidget *active_badge = NULL;
    GtkWidget *activate = gtk_button_new_from_icon_name("object-select-symbolic");
    GtkWidget *edit = gtk_button_new_from_icon_name("document-edit-symbolic");
    GtkWidget *remove = gtk_button_new_from_icon_name("user-trash-symbolic");
    GtkDragSource *drag_source;
    GtkDropTarget *drop_target;
    GtkDropControllerMotion *drop_motion;
    char *row_name = control_build_debug_name("engine-order-row", engine_name);
    char *subtitle_text = control_dup_engine_order_status(control, engine_name);
    guint active_index = GTK_INVALID_LIST_POSITION;
    const char *active_engine = NULL;

    control_name_widget(row, row_name);
    g_object_set_data_full(G_OBJECT(row), "typio-engine-name", g_strdup(engine_name), g_free);
    gtk_widget_add_css_class(shell, "preference-row");
    gtk_widget_add_css_class(shell, "drag-source");
    gtk_widget_set_hexpand(text_box, TRUE);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_widget_add_css_class(title, "preference-title");

    if (!subtitle_text || !*subtitle_text) {
        g_free(subtitle_text);
        subtitle_text = g_strdup_printf("Engine id: %s", engine_name);
    } else {
        char *with_id = g_strdup_printf("%s · id: %s", subtitle_text, engine_name);
        g_free(subtitle_text);
        subtitle_text = with_id;
    }
    subtitle = gtk_label_new(subtitle_text);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "preference-description");
    if (control && control->engine_dropdown && control->engine_id_model) {
        active_index = gtk_drop_down_get_selected(control->engine_dropdown);
        active_engine = control_string_array_get(control->engine_id_model, active_index);
    }
    if (g_strcmp0(active_engine, engine_name) == 0) {
        active_badge = gtk_label_new("Active");
        gtk_widget_add_css_class(active_badge, "engine-status-badge");
        gtk_widget_add_css_class(active_badge, "engine-status-badge-active");
        gtk_box_append(GTK_BOX(status_box), active_badge);
    }
    g_object_set_data_full(G_OBJECT(activate), "typio-engine-name", g_strdup(engine_name), g_free);
    control_name_widget(activate, "engine-activate-button");
    gtk_widget_add_css_class(activate, "flat");
    gtk_widget_add_css_class(activate, "engine-order-action-button");
    gtk_widget_set_valign(activate, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(activate, GTK_ALIGN_END);
    gtk_widget_set_tooltip_text(activate, "Set as active keyboard engine");
    gtk_widget_set_sensitive(activate, g_strcmp0(active_engine, engine_name) != 0);
    g_signal_connect(activate, "clicked", G_CALLBACK(on_engine_activate_clicked), control);
    g_object_set_data_full(G_OBJECT(edit), "typio-engine-name", g_strdup(engine_name), g_free);
    control_name_widget(edit, "engine-settings-edit-button");
    gtk_widget_add_css_class(edit, "flat");
    gtk_widget_add_css_class(edit, "engine-order-action-button");
    gtk_widget_set_valign(edit, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(edit, GTK_ALIGN_END);
    gtk_widget_set_tooltip_text(edit, "Show engine settings");
    g_signal_connect(edit, "clicked", G_CALLBACK(on_engine_settings_edit_clicked), control);
    g_object_set_data_full(G_OBJECT(remove), "typio-engine-name", g_strdup(engine_name), g_free);
    control_name_widget(remove, "engine-order-remove-button");
    gtk_widget_add_css_class(remove, "flat");
    gtk_widget_add_css_class(remove, "engine-order-action-button");
    gtk_widget_add_css_class(remove, "engine-order-remove-button");
    gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(remove, GTK_ALIGN_END);
    gtk_widget_set_tooltip_text(remove, "Remove from custom order");
    g_signal_connect(remove, "clicked", G_CALLBACK(on_engine_order_remove_clicked), control);

    gtk_box_append(GTK_BOX(text_box), title);
    gtk_box_append(GTK_BOX(text_box), subtitle);
    gtk_box_append(GTK_BOX(shell), text_box);
    gtk_box_append(GTK_BOX(shell), status_box);
    gtk_box_append(GTK_BOX(shell), activate);
    gtk_box_append(GTK_BOX(shell), edit);
    gtk_box_append(GTK_BOX(shell), remove);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), shell);

    drag_source = gtk_drag_source_new();
    gtk_drag_source_set_actions(drag_source, GDK_ACTION_MOVE);
    g_signal_connect(drag_source, "prepare",
                     G_CALLBACK(control_engine_order_drag_prepare), control);
    g_signal_connect(drag_source, "drag-begin",
                     G_CALLBACK(control_engine_order_drag_begin), control);
    g_signal_connect(drag_source, "drag-end",
                     G_CALLBACK(control_engine_order_drag_end), control);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(drag_source));

    drop_target = gtk_drop_target_new(G_TYPE_STRING, GDK_ACTION_MOVE);
    g_signal_connect(drop_target, "drop",
                     G_CALLBACK(control_engine_order_drop_cb), control);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(drop_target));

    drop_motion = GTK_DROP_CONTROLLER_MOTION(gtk_drop_controller_motion_new());
    g_signal_connect(drop_motion, "enter",
                     G_CALLBACK(control_engine_order_drag_motion_enter), control);
    g_signal_connect(drop_motion, "motion",
                     G_CALLBACK(control_engine_order_drag_motion), control);
    g_signal_connect(drop_motion, "leave",
                     G_CALLBACK(control_engine_order_drag_motion_leave), control);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(drop_motion));

    g_free(row_name);
    g_free(subtitle_text);
    return row;
}

static void control_clear_list_box(GtkListBox *list) {
    GtkWidget *child;

    if (!list) {
        return;
    }

    while ((child = gtk_widget_get_first_child(GTK_WIDGET(list))) != NULL) {
        gtk_list_box_remove(list, child);
    }
}

static gboolean control_has_custom_engine_order(TypioControl *control) {
    return control && control_string_list_count(control->engine_order_model) > 0;
}

static guint control_effective_engine_order_count(TypioControl *control) {
    guint custom_count;

    if (!control) {
        return 0;
    }

    custom_count = control_string_list_count(control->engine_order_model);
    return custom_count > 0 ? custom_count : control_string_array_count(control->engine_id_model);
}

static const char *control_effective_engine_order_name(TypioControl *control, guint index) {
    guint custom_count;

    if (!control) {
        return NULL;
    }

    custom_count = control_string_list_count(control->engine_order_model);
    if (custom_count > 0) {
        return gtk_string_list_get_string(control->engine_order_model, index);
    }

    return control_string_array_get(control->engine_id_model, index);
}

static void control_materialize_current_engine_order(TypioControl *control) {
    const char *items[2] = {NULL, NULL};

    if (!control || !control->engine_order_model || !control->engine_id_model ||
        control_has_custom_engine_order(control)) {
        return;
    }

    for (guint i = 0; i < control_string_array_count(control->engine_id_model); ++i) {
        char *copy = g_strdup(control_string_array_get(control->engine_id_model, i));
        if (!copy || !*copy) {
            g_free(copy);
            continue;
        }

        items[0] = copy;
        gtk_string_list_splice(control->engine_order_model,
                               control_string_list_count(control->engine_order_model),
                               0,
                               items);
        g_free(copy);
    }
}

static void control_update_engine_order_mode_ui(TypioControl *control) {
    gboolean has_custom;

    if (!control) {
        return;
    }

    has_custom = control_has_custom_engine_order(control);

    if (control->engine_order_mode_label) {
        gtk_label_set_text(
            control->engine_order_mode_label,
            has_custom
                ? "Custom order is active. Drag to reorder or remove engines from the custom list."
                : "Automatic order is active. This list is currently following the runtime engine discovery order.");
    }

    if (control->engine_order_reset_button) {
        gtk_widget_set_visible(GTK_WIDGET(control->engine_order_reset_button), has_custom);
        gtk_widget_set_sensitive(GTK_WIDGET(control->engine_order_reset_button), has_custom);
    }
}

static gboolean control_engine_order_refresh_idle_cb(gpointer user_data) {
    TypioControl *control = user_data;

    if (!control) {
        return G_SOURCE_REMOVE;
    }

    control->engine_order_refresh_source_id = 0;
    control_refresh_engine_order_editor(control);
    return G_SOURCE_REMOVE;
}

static void control_queue_engine_order_editor_refresh(TypioControl *control) {
    if (!control) {
        return;
    }

    if (control->engine_order_refresh_source_id != 0) {
        return;
    }

    control->engine_order_refresh_source_id =
        g_idle_add(control_engine_order_refresh_idle_cb, control);
}

void control_refresh_engine_order_editor(TypioControl *control) {
    guint order_count;

    if (!control || !control->engine_order_list ||
        !control->engine_order_model || !control->engine_order_add_model ||
        !control->engine_order_add_id_model) {
        return;
    }

    control_clear_list_box(control->engine_order_list);
    control_clear_string_list(control->engine_order_add_model);
    control_clear_string_array(control->engine_order_add_id_model);

    order_count = control_effective_engine_order_count(control);
    if (order_count == 0) {
        gtk_list_box_append(control->engine_order_list,
                            control_create_preference_row_named(
                                "keyboard-order-empty-row",
                                "No keyboard engines available",
                                "Typio could not find any keyboard engine to display right now.",
                                NULL));
    } else {
        for (guint i = 0; i < order_count; ++i) {
            const char *engine_name = control_effective_engine_order_name(control, i);
            if (!engine_name || !*engine_name) {
                continue;
            }
            gtk_list_box_append(control->engine_order_list,
                                control_build_engine_order_row(control,
                                                               engine_name,
                                                               i,
                                                               order_count));
        }
    }

    for (guint i = 0; i < control_string_array_count(control->engine_id_model); ++i) {
        const char *engine_name = control_string_array_get(control->engine_id_model, i);
        if (!engine_name || !*engine_name ||
            control_string_list_contains(control->engine_order_model, engine_name)) {
            continue;
        }
        g_ptr_array_add(control->engine_order_add_id_model, g_strdup(engine_name));
        gtk_string_list_append(control->engine_order_add_model,
                               typio_engine_label_fallback(engine_name));
    }

    if (control->engine_order_add_dropdown) {
        guint add_count = control_string_list_count(control->engine_order_add_model);
        gtk_widget_set_sensitive(GTK_WIDGET(control->engine_order_add_dropdown), add_count > 0);
        gtk_drop_down_set_selected(control->engine_order_add_dropdown,
                                   add_count > 0 ? 0 : GTK_INVALID_LIST_POSITION);
    }

    control_update_engine_order_mode_ui(control);
}

void control_load_engine_order_from_config(TypioControl *control,
                                           const TypioConfig *config) {
    size_t order_count;

    if (!control || !control->engine_order_model) {
        return;
    }

    control_clear_string_list(control->engine_order_model);
    order_count = config ? typio_config_get_array_size(config, "engine_order") : 0;
    for (size_t i = 0; i < order_count; ++i) {
        const char *engine_name = typio_config_get_array_string(config, "engine_order", i);
        control_append_unique_string(control->engine_order_model, engine_name);
    }

    control_refresh_engine_order_editor(control);
}

static char *control_dup_runtime_string_for_config_key(TypioControl *control,
                                                       const char *config_key) {
    GVariant *value;
    const char *text;
    char *result = NULL;

    value = control_get_runtime_property_for_config_key(control, config_key);
    if (!value || !g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
        if (value) {
            g_variant_unref(value);
        }
        return NULL;
    }

    text = g_variant_get_string(value, NULL);
    if (text && *text) {
        result = g_strdup(text);
    }
    g_variant_unref(value);
    return result;
}

static char *control_dup_state_binding_value(TypioControl *control,
                                             const ControlStateBinding *binding,
                                             const char *fallback_text) {
    if (!binding || !binding->config_key) {
        return NULL;
    }

    switch (binding->source) {
    case CONTROL_STATE_VALUE_FROM_CONFIG:
        return control_dup_staged_config_string(control, binding->config_key, fallback_text);
    case CONTROL_STATE_VALUE_FROM_RUNTIME:
        return control_dup_runtime_string_for_config_key(control, binding->config_key);
    case CONTROL_STATE_VALUE_RUNTIME_THEN_CONFIG:
        {
            char *result = control_dup_runtime_string_for_config_key(control, binding->config_key);
            if (result) {
                return result;
            }
            return control_dup_staged_config_string(control, binding->config_key, fallback_text);
        }
    }

    return NULL;
}

static void control_apply_state_binding_value(TypioControl *control,
                                              const ControlStateBinding *binding,
                                              const char *fallback_text) {
    char *value;
    gboolean was_updating_ui;

    if (!control || !binding) {
        return;
    }

    was_updating_ui = control->updating_ui;
    control->updating_ui = TRUE;
    value = control_dup_state_binding_value(control, binding, fallback_text);
    control_state_binding_select_value(binding, value);
    control->updating_ui = was_updating_ui;
    g_free(value);
}

#ifdef TYPIO_CONTROL_TEST
void control_test_apply_state_binding_value(TypioControl *control,
                                            const ControlStateBinding *binding,
                                            const char *fallback_text) {
    control_apply_state_binding_value(control, binding, fallback_text);
}

void control_test_set_config_text(TypioControl *control,
                                  GVariant *config_text) {
    control_set_config_text(control, config_text);
}

void control_test_set_engine_model(TypioControl *control,
                                   GVariant *engines,
                                   GVariant *display_names) {
    control_set_engine_model(control, engines, display_names);
}

void control_test_set_voice_backend_model(TypioControl *control,
                                          GVariant *engines) {
    control_set_voice_backend_model(control, engines);
}
#endif

static gboolean control_is_ui_syncing(TypioControl *control) {
    return control && control->updating_ui;
}

static void control_begin_ui_sync(TypioControl *control) {
    if (control) {
        control->updating_ui = TRUE;
    }
}

static void control_end_ui_sync(TypioControl *control) {
    if (control) {
        control->updating_ui = FALSE;
    }
}

static const char *control_resolve_config_text(const char *text) {
    return text ? text : "";
}

void control_queue_autosave(TypioControl *control,
                            ControlAutosavePriority priority);

static void control_replace_staged_config_text(TypioControl *control,
                                               const char *text) {
    const char *resolved = control_resolve_config_text(text);

    if (!control || !control->config_buffer) {
        return;
    }

    control_begin_ui_sync(control);
    gtk_text_buffer_set_text(control->config_buffer, resolved, -1);
    control_sync_form_from_buffer(control);
    control_end_ui_sync(control);
}

static void control_set_committed_config_text(TypioControl *control,
                                              const char *text) {
    if (!control) {
        return;
    }

    g_free(control->committed_config_text);
    control->committed_config_text = g_strdup(control_resolve_config_text(text));
}

static void control_set_inline_status(TypioControl *control,
                                      const char *text,
                                      gboolean visible) {
    if (!control || !control->config_status_label) {
        return;
    }

    gtk_label_set_text(control->config_status_label, text ? text : "");
    gtk_widget_set_visible(GTK_WIDGET(control->config_status_label), visible);
}

static gboolean control_clear_status_timeout_cb(gpointer user_data) {
    TypioControl *control = user_data;

    if (!control) {
        return G_SOURCE_REMOVE;
    }

    control->status_clear_source_id = 0;
    control_set_inline_status(control, "", FALSE);
    return G_SOURCE_REMOVE;
}

static void control_schedule_status_clear(TypioControl *control, guint delay_ms) {
    if (!control) {
        return;
    }

    if (control->status_clear_source_id != 0) {
        g_source_remove(control->status_clear_source_id);
        control->status_clear_source_id = 0;
    }

    if (delay_ms == 0) {
        control_set_inline_status(control, "", FALSE);
        return;
    }

    control->status_clear_source_id =
        g_timeout_add(delay_ms, control_clear_status_timeout_cb, control);
}

gboolean control_has_pending_config_change(TypioControl *control) {
    char *content;
    gboolean pending = FALSE;

    if (!control || !control->config_buffer || !control->config_seeded ||
        !control->committed_config_text) {
        return FALSE;
    }

    content = control_dup_buffer_text(control);
    if (!content) {
        return FALSE;
    }

    pending = !control->committed_config_text ||
              g_strcmp0(content, control->committed_config_text) != 0;
    g_free(content);
    return pending;
}

static void control_update_engine_config_panel(TypioControl *control,
                                               const char *engine_name) {
    const char *visible_engine = NULL;
    const char *display_name = NULL;
    char *title = NULL;

    if (!control || !control->engine_config_stack) {
        return;
    }

    visible_engine = (control->engine_settings_engine && *control->engine_settings_engine)
        ? control->engine_settings_engine
        : engine_name;

    if (visible_engine &&
        gtk_stack_get_child_by_name(control->engine_config_stack, visible_engine)) {
        gtk_stack_set_visible_child_name(control->engine_config_stack, visible_engine);
    } else {
        gtk_stack_set_visible_child_name(control->engine_config_stack, "empty");
    }

    if (control->engine_settings_window) {
        display_name = typio_engine_label_fallback(visible_engine);
        title = g_strdup_printf("%s settings",
                                display_name ? display_name : "selected engine");
        gtk_window_set_title(control->engine_settings_window, title);
        g_free(title);
    }
}

static void control_set_config_text(TypioControl *control, GVariant *config_text) {
    const char *text = "";
    gboolean should_replace_stage;
    gboolean has_pending_local_changes;
    gboolean has_local_stage;
    char *staged_text;
    char *preferred_voice_backend;

    if (!control || !control->config_buffer) {
        return;
    }

    if (!config_text || !g_variant_is_of_type(config_text, G_VARIANT_TYPE_STRING)) {
        g_debug("control_set_config_text: skipping seed because ConfigText is unavailable");
        return;
    }

    text = g_variant_get_string(config_text, nullptr);

    staged_text = control_dup_buffer_text(control);
    has_local_stage = staged_text && *staged_text;
    has_pending_local_changes = has_local_stage &&
        control->committed_config_text &&
        g_strcmp0(staged_text, control->committed_config_text) != 0;
    should_replace_stage = !has_pending_local_changes ||
        (staged_text && g_strcmp0(staged_text, text) == 0);

    g_debug("control_set_config_text: submitting=%d committed_matches=%d replace_stage=%d",
            control->submitting_config,
            control->committed_config_text &&
                g_strcmp0(control->committed_config_text, text) == 0,
            should_replace_stage);
    g_free(staged_text);
    preferred_voice_backend = control_dup_staged_config_string(control,
                                                               control->voice_backend_state.config_key,
                                                               text);
    g_debug("control_set_config_text: incoming default_voice_engine=%s",
            preferred_voice_backend ? preferred_voice_backend : "(unset)");
    g_free(preferred_voice_backend);

    control->config_seeded = TRUE;

    control_set_committed_config_text(control, text);
    if (should_replace_stage) {
        control_replace_staged_config_text(control, text);
        return;
    }
}

static void control_append_rime_schema(TypioControl *control, const char *schema_id) {
    guint count;

    if (!control || !control->rime_schema_model || !control->rime_schema_id_model ||
        !schema_id || !*schema_id) {
        return;
    }

    count = control_string_array_count(control->rime_schema_id_model);
    for (guint i = 0; i < count; ++i) {
        const char *item = control_string_array_get(control->rime_schema_id_model, i);
        if (item && g_strcmp0(item, schema_id) == 0) {
            return;
        }
    }

    g_ptr_array_add(control->rime_schema_id_model, g_strdup(schema_id));
    gtk_string_list_append(control->rime_schema_model, schema_id);
}

static void control_clear_rime_schema_model(TypioControl *control) {
    guint count;

    if (!control || !control->rime_schema_model || !control->rime_schema_id_model) {
        return;
    }

    control_clear_string_array(control->rime_schema_id_model);
    count = (guint)g_list_model_get_n_items(G_LIST_MODEL(control->rime_schema_model));
    gtk_string_list_splice(control->rime_schema_model, 0, count, NULL);
}

void control_refresh_rime_schema_options(gpointer user_data,
                                         const TypioConfig *parsed_config,
                                         const char *configured_schema) {
    TypioControl *control = user_data;
    gboolean was_updating_ui;
    TypioConfig *rime_config = NULL;
    TypioRimeSchemaList list;
    const char *default_data_dir = NULL;
    char *data_dir_buf = NULL;

    if (!control || !control->rime_schema_model || !control->rime_schema_id_model) {
        return;
    }

    was_updating_ui = control_is_ui_syncing(control);
    control_begin_ui_sync(control);
    control_clear_rime_schema_model(control);
    g_ptr_array_add(control->rime_schema_id_model, g_strdup(""));
    gtk_string_list_append(control->rime_schema_model, "Unselected");

    if (parsed_config) {
        rime_config = typio_config_get_section(parsed_config, "engines.rime");
        default_data_dir = typio_config_get_string(parsed_config,
                                                   "engines.rime.user_data_dir",
                                                   NULL);
    }

    if (!default_data_dir) {
        const char *data_home = g_get_user_data_dir();
        if (data_home) {
            data_dir_buf = g_build_filename(data_home, "typio", "rime", NULL);
            default_data_dir = data_dir_buf;
        }
    }

    if (typio_rime_schema_list_load(rime_config, default_data_dir, &list)) {
        for (size_t i = 0; i < list.schema_count; ++i) {
            g_debug("control_refresh_rime_schema_model: schema=%s",
                    list.schemas[i].id ? list.schemas[i].id : "(null)");
            control_append_rime_schema(control, list.schemas[i].id);
        }
        typio_rime_schema_list_clear(&list);
    }

    if (configured_schema && *configured_schema) {
        control_append_rime_schema(control, configured_schema);
    }

    g_debug("control_refresh_rime_schema_model: configured_schema=%s count=%u",
            configured_schema ? configured_schema : "(null)",
            (guint)g_list_model_get_n_items(G_LIST_MODEL(control->rime_schema_model)));
    gtk_widget_set_sensitive(GTK_WIDGET(control->rime_schema_dropdown), TRUE);
    control->updating_ui = was_updating_ui;

    if (rime_config) {
        typio_config_free(rime_config);
    }
    g_free(data_dir_buf);
}

static void control_select_voice_model_from_config(TypioControl *control,
                                                   TypioConfig *config) {
    guint backend_idx;
    const char *backend_name;
    char engine_model_key[256];
    const char *voice_model;
    guint idx;

    if (!control || !config || !control->voice_model_dropdown ||
        !control->voice_model_list || !control->voice_backend_dropdown) {
        return;
    }

    backend_idx = gtk_drop_down_get_selected(control->voice_backend_dropdown);
    backend_name = control_voice_backend_id(control, backend_idx);
    if (!backend_name) {
        return;
    }
    g_snprintf(engine_model_key, sizeof(engine_model_key),
               "engines.%s.model", backend_name);
    voice_model = typio_config_get_string(config, engine_model_key, "");
    idx = control_find_model_index(control->voice_model_list, voice_model);
    gtk_drop_down_set_selected(control->voice_model_dropdown, idx);
}

void control_refresh_voice_models_from_stage(TypioControl *control) {
    char *content;
    TypioConfig *config;

    if (!control) {
        return;
    }

    control_refresh_voice_models(control);

    content = control_dup_buffer_text(control);
    config = content ? typio_config_load_string(content) : nullptr;
    g_free(content);
    if (!config) {
        g_warning("control_sync_form_from_buffer: failed to parse staged config");
        return;
    }

    control_select_voice_model_from_config(control, config);
    typio_config_free(config);
}

void control_update_availability_label(TypioControl *control,
                                       const char *message,
                                       gboolean visible) {
    if (!control) {
        return;
    }

    (void) message;
    (void) visible;
}

void control_sync_form_from_buffer(TypioControl *control) {
    GtkTextIter start;
    GtkTextIter end;
    char *content;
    TypioConfig *config;
    const char *configured_schema;

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

    control_begin_ui_sync(control);
    control_bindings_load_all(control->bindings, control->binding_count, config);
    control_load_engine_order_from_config(control, config);
    configured_schema = typio_config_get_string(config,
                                                control->rime_schema_state.config_key,
                                                NULL);
    control_state_binding_refresh_options(&control->rime_schema_state,
                                          config,
                                          configured_schema);
    control_state_binding_load_from_config(&control->rime_schema_state, config);
    if (control->voice_backend_dropdown) {
        const char *voice_backend = typio_config_get_string(config,
                                                            control->voice_backend_state.config_key,
                                                            nullptr);
        g_debug("control_sync_form_from_buffer: default_voice_engine=%s voice_backend_index=%u",
                voice_backend ? voice_backend : "(unset)",
                control_voice_backend_index(control, voice_backend));
        control_state_binding_load_from_config(&control->voice_backend_state, config);
    }
    voice_update_model_sections(control);
    control_refresh_voice_models_from_stage(control);

    if (control->shortcut_switch_engine_btn) {
        gtk_button_set_label(control->shortcut_switch_engine_btn,
                             typio_config_get_string(config,
                                                     "shortcuts.switch_engine",
                                                     "Ctrl+Shift"));
    }
    if (control->shortcut_voice_ptt_btn) {
        gtk_button_set_label(control->shortcut_voice_ptt_btn,
                             typio_config_get_string(config,
                                                     "shortcuts.voice_ptt",
                                                     "Super+v"));
    }

    control_end_ui_sync(control);
    typio_config_free(config);
}

void control_sync_buffer_from_form(TypioControl *control) {
    GtkTextIter start;
    GtkTextIter end;
    char *content;
    char *rendered;
    TypioConfig *config;
    guint selected;
    const char *voice_backend;
    gboolean has_voice_backend_selection;

    if (!control || !control->config_buffer || control_is_ui_syncing(control)) {
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
        g_warning("control_sync_buffer_from_form: failed to load or create config");
        return;
    }

    control_bindings_save_all(control->bindings, control->binding_count, config);

    if (control->engine_order_model) {
        guint order_count = control_string_list_count(control->engine_order_model);
        if (order_count > 0) {
            const char **ordered_names = g_new0(const char *, order_count);
            for (guint i = 0; i < order_count; ++i) {
                ordered_names[i] = gtk_string_list_get_string(control->engine_order_model, i);
            }
            typio_config_set_string_array(config, "engine_order", ordered_names, order_count);
            g_free(ordered_names);
        } else {
            typio_config_remove(config, "engine_order");
        }
    }

    if (control->rime_schema_dropdown && control->rime_schema_model) {
        const char *schema = control_state_binding_get_selected_value(&control->rime_schema_state);
        if (schema && *schema) {
            g_debug("control_sync_buffer_from_form: selected_rime_schema=%s", schema);
            control_state_binding_save_to_config(&control->rime_schema_state, config);
        } else {
            typio_config_remove(config, control->rime_schema_state.config_key);
        }
    }

    selected = gtk_drop_down_get_selected(control->voice_backend_dropdown);
    voice_backend = control_voice_backend_id(control, selected);
    has_voice_backend_selection = voice_backend && *voice_backend;
    g_debug("control_sync_buffer_from_form: voice_backend=%s (dropdown=%u)",
            voice_backend ? voice_backend : "(unset)", selected);
    if (has_voice_backend_selection) {
        typio_config_remove(config, "voice.backend");
        typio_config_remove(config, "voice.model");
        control_state_binding_save_to_config(&control->voice_backend_state, config);

        selected = gtk_drop_down_get_selected(control->voice_model_dropdown);
        if (selected != GTK_INVALID_LIST_POSITION) {
            const char *voice_model = gtk_string_list_get_string(control->voice_model_list, selected);
            if (voice_model && *voice_model) {
                char engine_model_key[256];
                g_snprintf(engine_model_key, sizeof(engine_model_key),
                           "engines.%s.model", voice_backend);
                typio_config_set_string(config, engine_model_key, voice_model);
            }
        }
    }

    if (control->shortcut_switch_engine_btn) {
        const char *val = gtk_button_get_label(control->shortcut_switch_engine_btn);
        if (val && *val) {
            typio_config_set_string(config, "shortcuts.switch_engine", val);
        }
    }
    if (control->shortcut_voice_ptt_btn) {
        const char *val = gtk_button_get_label(control->shortcut_voice_ptt_btn);
        if (val && *val) {
            typio_config_set_string(config, "shortcuts.voice_ptt", val);
        }
    }

    rendered = typio_config_to_string(config);
    typio_config_free(config);
    if (!rendered) {
        return;
    }

    control_begin_ui_sync(control);
    gtk_text_buffer_set_text(control->config_buffer, rendered, -1);
    control_end_ui_sync(control);
    free(rendered);
}

static void control_set_engine_model(TypioControl *control,
                                     GVariant *engines,
                                     GVariant *display_names) {
    guint count = 0;

    if (!control || !control->engine_model || !control->engine_id_model) {
        return;
    }

    control_begin_ui_sync(control);
    count = (guint)g_list_model_get_n_items(G_LIST_MODEL(control->engine_model));
    gtk_string_list_splice(control->engine_model, 0, count, nullptr);
    control_clear_string_array(control->engine_id_model);

    if (engines && g_variant_is_of_type(engines, G_VARIANT_TYPE("as"))) {
        GVariantIter iter;
        const char *name;

        g_variant_iter_init(&iter, engines);
        while (g_variant_iter_next(&iter, "&s", &name)) {
            const char *display_name;
            if (is_voice_backend_name(name)) {
                continue;
            }
            g_ptr_array_add(control->engine_id_model, g_strdup(name));
            display_name = control_lookup_engine_display_name(display_names, name);
            gtk_string_list_append(control->engine_model,
                                   display_name ? display_name : typio_engine_label_fallback(name));
        }
    } else {
        gtk_drop_down_set_selected(control->engine_dropdown, GTK_INVALID_LIST_POSITION);
    }

    control_end_ui_sync(control);
    control_refresh_engine_order_editor(control);
}

static void control_set_voice_backend_model(TypioControl *control,
                                            GVariant *engines) {
    guint count;

    if (!control || !control->voice_backend_model || !control->voice_backend_dropdown) {
        return;
    }

    control_begin_ui_sync(control);
    count = (guint)g_list_model_get_n_items(G_LIST_MODEL(control->voice_backend_model));
    gtk_string_list_splice(control->voice_backend_model, 0, count, nullptr);

    if (engines && g_variant_is_of_type(engines, G_VARIANT_TYPE("as"))) {
        GVariantIter iter;
        const char *name;

        g_variant_iter_init(&iter, engines);
        while (g_variant_iter_next(&iter, "&s", &name)) {
            if (is_voice_backend_name(name)) {
                g_debug("control_set_voice_backend_model: append backend=%s", name);
                gtk_string_list_append(control->voice_backend_model, name);
            }
        }
    }
    g_debug("control_set_voice_backend_model: n_items=%u",
            (guint)g_list_model_get_n_items(G_LIST_MODEL(control->voice_backend_model)));
    control_end_ui_sync(control);
}

void control_refresh_from_proxy(TypioControl *control) {
    GVariant *active_engine;
    GVariant *available_engines;
    GVariant *ordered_engines;
    GVariant *engine_display_names;
    GVariant *config_text;
    const char *active_name = "";
    const char *config_text_str = NULL;
    TypioConfig *parsed_config = NULL;
    const char *configured_schema = NULL;

    if (!control) {
        return;
    }

    if (!control->proxy || !g_dbus_proxy_get_name_owner(control->proxy)) {
        g_warning("control_refresh_from_proxy: Typio service unavailable");
        control_update_availability_label(control, "Typio service unavailable", TRUE);
        control_clear_rime_schema_model(control);
        control_set_engine_model(control, nullptr, nullptr);
        control_set_voice_backend_model(control, nullptr);
        control_apply_state_binding_value(control, &control->keyboard_engine_state, NULL);
        control_apply_state_binding_value(control, &control->voice_backend_state, NULL);
        gtk_widget_set_sensitive(GTK_WIDGET(control->engine_dropdown), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(control->voice_backend_dropdown), FALSE);
        return;
    }

    control_update_availability_label(control, "", FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(control->engine_dropdown), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(control->voice_backend_dropdown), TRUE);

    active_engine = control_get_runtime_property_for_config_key(control,
                                                               control->keyboard_engine_state.config_key);
    available_engines = g_dbus_proxy_get_cached_property(control->proxy,
                                                         TYPIO_STATUS_PROP_AVAILABLE_ENGINES);
    ordered_engines = g_dbus_proxy_get_cached_property(control->proxy,
                                                       TYPIO_STATUS_PROP_ORDERED_ENGINES);
    engine_display_names = g_dbus_proxy_get_cached_property(control->proxy,
                                                            TYPIO_STATUS_PROP_ENGINE_DISPLAY_NAMES);
    config_text = g_dbus_proxy_get_cached_property(control->proxy, TYPIO_STATUS_PROP_CONFIG_TEXT);

    if (active_engine) {
        active_name = g_variant_get_string(active_engine, NULL);
    }

    if (config_text && g_variant_is_of_type(config_text, G_VARIANT_TYPE_STRING)) {
        config_text_str = g_variant_get_string(config_text, NULL);
        parsed_config = typio_config_load_string(config_text_str);
        configured_schema = parsed_config
            ? typio_config_get_string(parsed_config, control->rime_schema_state.config_key, NULL)
            : NULL;
    }

    g_debug("control_refresh_from_proxy: entering");
    if (parsed_config) {
        const char *configured_voice =
            typio_config_get_string(parsed_config, control->voice_backend_state.config_key, NULL);
        g_debug("control_refresh_from_proxy: config default_voice_engine=%s configured_schema=%s",
                configured_voice ? configured_voice : "(unset)",
                configured_schema ? configured_schema : "(unset)");
    }
    control_set_engine_model(control, ordered_engines, engine_display_names);
    control_state_binding_refresh_options(&control->rime_schema_state,
                                          parsed_config,
                                          configured_schema);
    control_set_voice_backend_model(control, available_engines);
    control_apply_state_binding_value(control, &control->keyboard_engine_state, NULL);
    control_apply_state_binding_value(
        control,
        &control->voice_backend_state,
        config_text && g_variant_is_of_type(config_text, G_VARIANT_TYPE_STRING)
            ? g_variant_get_string(config_text, NULL)
            : NULL);
    g_debug("control_refresh_from_proxy: active_engine has_config=%d has_engines=%d",
            config_text != NULL,
            available_engines != NULL);
    control_update_engine_config_panel(control, active_name);
    control_set_config_text(control, config_text);

    if (config_text) {
        g_variant_unref(config_text);
    }
    if (available_engines) {
        g_variant_unref(available_engines);
    }
    if (ordered_engines) {
        g_variant_unref(ordered_engines);
    }
    if (engine_display_names) {
        g_variant_unref(engine_display_names);
    }
    if (active_engine) {
        g_variant_unref(active_engine);
    }
    if (parsed_config) {
        typio_config_free(parsed_config);
    }
}

static void control_activate_engine(TypioControl *control, const char *engine_name) {
    GError *error = nullptr;
    GVariant *reply;

    if (!control || !control->proxy || !engine_name || !*engine_name) {
        return;
    }

    reply = g_dbus_proxy_call_sync(control->proxy,
                                   TYPIO_STATUS_METHOD_ACTIVATE_ENGINE,
                                   g_variant_new("(s)", engine_name),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   nullptr,
                                   &error);
    if (!reply) {
        g_warning("control_activate_engine: engine=%s failed: %s",
                  engine_name, error ? error->message : "Engine switch failed");
        control_update_availability_label(control,
                                          error ? error->message : "Engine switch failed",
                                          TRUE);
        g_clear_error(&error);
        return;
    }

    g_variant_unref(reply);
}

static void on_set_config_text_finished(GObject *source,
                                        GAsyncResult *result,
                                        gpointer user_data) {
    TypioControl *control = user_data;
    GError *error = nullptr;
    GVariant *reply;

    if (!control) {
        return;
    }

    reply = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), result, &error);
    if (!reply) {
        g_warning("on_set_config_text_finished: failed: %s",
                  error ? error->message : "Failed to apply configuration");
        control->submitting_config = FALSE;
        control_set_inline_status(control,
                                  "Unable to apply changes right now.",
                                  TRUE);
        control_schedule_status_clear(control, 3000);
        control_update_availability_label(control,
                                          error ? error->message
                                                : "Failed to apply configuration",
                                          TRUE);
        control_queue_autosave(control, CONTROL_AUTOSAVE_NORMAL);
        g_clear_error(&error);
        return;
    }

    g_variant_unref(reply);
    control->submitting_config = FALSE;
    g_debug("on_set_config_text_finished: success, refreshing from proxy");
    control_schedule_status_clear(control, 0);
    control_refresh_from_proxy(control);
}

static gboolean control_autosave_timeout_cb(gpointer user_data) {
    TypioControl *control = user_data;
    char *content;

    if (!control) {
        return G_SOURCE_REMOVE;
    }

    control->autosave_source_id = 0;

    if (!control->proxy || !g_dbus_proxy_get_name_owner(control->proxy)) {
        control_queue_autosave(control, CONTROL_AUTOSAVE_NORMAL);
        return G_SOURCE_REMOVE;
    }

    if (control->submitting_config || !control->config_buffer) {
        control_queue_autosave(control, CONTROL_AUTOSAVE_NORMAL);
        return G_SOURCE_REMOVE;
    }

    content = control_dup_buffer_text(control);
    if (!content) {
        return G_SOURCE_REMOVE;
    }

    if (control->committed_config_text &&
        g_strcmp0(content, control->committed_config_text) == 0) {
        g_free(content);
        control_schedule_status_clear(control, 0);
        return G_SOURCE_REMOVE;
    }

    control->submitting_config = TRUE;
    g_dbus_proxy_call(control->proxy,
                      TYPIO_STATUS_METHOD_SET_CONFIG_TEXT,
                      g_variant_new("(s)", content),
                      G_DBUS_CALL_FLAGS_NONE,
                      -1,
                      nullptr,
                      on_set_config_text_finished,
                      control);
    g_free(content);
    return G_SOURCE_REMOVE;
}

void control_queue_autosave(TypioControl *control,
                            ControlAutosavePriority priority) {
    guint delay_ms;

    if (!control) {
        return;
    }

    if (control->autosave_source_id != 0) {
        g_source_remove(control->autosave_source_id);
    }

    delay_ms = (priority == CONTROL_AUTOSAVE_FAST) ? 75 : 250;

    if (control->submitting_config ||
        !control->proxy ||
        !g_dbus_proxy_get_name_owner(control->proxy)) {
        delay_ms = 1000;
    }

    control->autosave_source_id = g_timeout_add(delay_ms, control_autosave_timeout_cb, control);
}

void control_stage_form_change(TypioControl *control,
                               ControlAutosavePriority priority) {
    if (!control || !control->config_seeded) {
        return;
    }

    control_sync_buffer_from_form(control);
    control_queue_autosave(control, priority);
}

void on_form_spin_changed([[maybe_unused]] GtkSpinButton *spin, gpointer user_data) {
    control_stage_form_change((TypioControl *) user_data, CONTROL_AUTOSAVE_NORMAL);
}

void on_voice_backend_changed(GObject *object,
                              [[maybe_unused]] GParamSpec *pspec,
                              gpointer user_data) {
    TypioControl *control = user_data;

    if (!control_is_ui_syncing(control) && GTK_DROP_DOWN(object)) {
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
        g_debug("on_voice_backend_changed: user changed dropdown to %u (%s)",
                sel, control_voice_backend_id(control, sel));
        voice_update_model_sections(control);
        control_refresh_voice_models_from_stage(control);
        control_stage_form_change(control, CONTROL_AUTOSAVE_FAST);
    }
}

void on_display_dropdown_changed(GObject *object,
                                 [[maybe_unused]] GParamSpec *pspec,
                                 gpointer user_data) {
    TypioControl *control = user_data;

    if (!control_is_ui_syncing(control) && GTK_DROP_DOWN(object)) {
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
        const char *schema = NULL;
        if (control->rime_schema_model && object == G_OBJECT(control->rime_schema_dropdown) &&
            selected != GTK_INVALID_LIST_POSITION) {
            schema = control_state_binding_get_selected_value(&control->rime_schema_state);
            g_debug("on_display_dropdown_changed: rime_schema_selected=%s",
                    schema ? schema : "(unselected)");
        }
        control_stage_form_change(control, CONTROL_AUTOSAVE_FAST);
    }
}

void on_display_spin_changed([[maybe_unused]] GtkSpinButton *spin, gpointer user_data) {
    TypioControl *control = user_data;

    if (!control_is_ui_syncing(control)) {
        control_stage_form_change(control, CONTROL_AUTOSAVE_NORMAL);
    }
}

void on_display_switch_changed(GObject *object,
                               [[maybe_unused]] GParamSpec *pspec,
                               gpointer user_data) {
    TypioControl *control = user_data;

    if (!control_is_ui_syncing(control) && GTK_IS_SWITCH(object)) {
        control_stage_form_change(control, CONTROL_AUTOSAVE_FAST);
    }
}

void on_display_entry_changed([[maybe_unused]] GtkEditable *editable,
                              gpointer user_data) {
    TypioControl *control = user_data;

    if (!control_is_ui_syncing(control)) {
        control_stage_form_change(control, CONTROL_AUTOSAVE_NORMAL);
    }
}

static guint control_engine_order_index(TypioControl *control, const char *engine_name) {
    if (!control || !control->engine_order_model) {
        return GTK_INVALID_LIST_POSITION;
    }

    return control_find_model_index(control->engine_order_model, engine_name);
}

static void control_engine_order_move(TypioControl *control,
                                      const char *engine_name,
                                      int direction) {
    guint index;
    guint count;
    guint target;
    char *owned_name;
    const char *items[2] = {NULL, NULL};

    if (!control || !engine_name || !*engine_name || !control->engine_order_model) {
        return;
    }

    control_materialize_current_engine_order(control);
    index = control_engine_order_index(control, engine_name);
    count = control_string_list_count(control->engine_order_model);
    if (index == GTK_INVALID_LIST_POSITION || count < 2) {
        return;
    }

    if (direction < 0) {
        if (index == 0) {
            return;
        }
        target = index - 1;
    } else {
        if (index + 1 >= count) {
            return;
        }
        target = index + 1;
    }

    owned_name = g_strdup(engine_name);
    gtk_string_list_splice(control->engine_order_model, index, 1, NULL);
    items[0] = owned_name;
    gtk_string_list_splice(control->engine_order_model, target, 0, items);
    g_free(owned_name);
    control_queue_engine_order_editor_refresh(control);
    control_stage_form_change(control, CONTROL_AUTOSAVE_FAST);
}

void on_engine_order_reset_clicked([[maybe_unused]] GtkButton *button, gpointer user_data) {
    TypioControl *control = user_data;
    guint count;

    if (!control || !control->engine_order_model) {
        return;
    }

    count = control_string_list_count(control->engine_order_model);
    if (count == 0) {
        return;
    }

    gtk_string_list_splice(control->engine_order_model, 0, count, NULL);
    control_queue_engine_order_editor_refresh(control);
    control_stage_form_change(control, CONTROL_AUTOSAVE_FAST);
}

void on_engine_order_add_clicked([[maybe_unused]] GtkButton *button, gpointer user_data) {
    TypioControl *control = user_data;
    guint selected;
    const char *engine_name;
    char *owned_name;
    const char *items[2] = {NULL, NULL};

    if (!control || !control->engine_order_add_dropdown || !control->engine_order_model) {
        return;
    }

    selected = gtk_drop_down_get_selected(control->engine_order_add_dropdown);
    if (selected == GTK_INVALID_LIST_POSITION) {
        return;
    }

    engine_name = control_string_array_get(control->engine_order_add_id_model, selected);
    if (!engine_name || !*engine_name ||
        control_string_list_contains(control->engine_order_model, engine_name)) {
        return;
    }

    control_materialize_current_engine_order(control);

    owned_name = g_strdup(engine_name);
    items[0] = owned_name;
    gtk_string_list_splice(control->engine_order_model,
                           control_string_list_count(control->engine_order_model),
                           0,
                           items);
    g_free(owned_name);
    control_queue_engine_order_editor_refresh(control);
    control_stage_form_change(control, CONTROL_AUTOSAVE_FAST);
}

void on_engine_order_move_up_clicked(GtkButton *button, gpointer user_data) {
    const char *engine_name = g_object_get_data(G_OBJECT(button), "typio-engine-name");
    control_engine_order_move(user_data, engine_name, -1);
}

void on_engine_order_move_down_clicked(GtkButton *button, gpointer user_data) {
    const char *engine_name = g_object_get_data(G_OBJECT(button), "typio-engine-name");
    control_engine_order_move(user_data, engine_name, 1);
}

void on_engine_order_remove_clicked(GtkButton *button, gpointer user_data) {
    TypioControl *control = user_data;
    const char *engine_name = g_object_get_data(G_OBJECT(button), "typio-engine-name");
    guint index;

    if (!control || !engine_name || !*engine_name || !control->engine_order_model) {
        return;
    }

    control_materialize_current_engine_order(control);
    index = control_engine_order_index(control, engine_name);
    if (index == GTK_INVALID_LIST_POSITION) {
        return;
    }

    gtk_string_list_splice(control->engine_order_model, index, 1, NULL);
    control_queue_engine_order_editor_refresh(control);
    control_stage_form_change(control, CONTROL_AUTOSAVE_FAST);
}

void on_engine_activate_clicked(GtkButton *button, gpointer user_data) {
    TypioControl *control = user_data;
    const char *engine_name = g_object_get_data(G_OBJECT(button), "typio-engine-name");

    if (!control || !engine_name || !*engine_name) {
        return;
    }

    control_activate_engine(control, engine_name);
}

void on_engine_settings_edit_clicked(GtkButton *button, gpointer user_data) {
    TypioControl *control = user_data;
    const char *engine_name = g_object_get_data(G_OBJECT(button), "typio-engine-name");

    if (!control || !engine_name || !*engine_name) {
        return;
    }

    g_free(control->engine_settings_engine);
    control->engine_settings_engine = g_strdup(engine_name);
    control_update_engine_config_panel(control, engine_name);
    if (control->engine_settings_window) {
        if (control->window) {
            gtk_window_set_transient_for(control->engine_settings_window,
                                         GTK_WINDOW(control->window));
        }
        gtk_window_present(control->engine_settings_window);
    }
}

gboolean on_engine_settings_window_close_request(GtkWindow *window, gpointer user_data) {
    TypioControl *control = user_data;

    if (!control) {
        return FALSE;
    }

    g_clear_pointer(&control->engine_settings_engine, g_free);
    control_update_engine_config_panel(control, NULL);
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
}

void on_engine_selected(GObject *object,
                        [[maybe_unused]] GParamSpec *pspec,
                        gpointer user_data) {
    TypioControl *control = user_data;
    guint selected;
    const char *engine_name;

    if (!control || control_is_ui_syncing(control)) {
        return;
    }

    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected == GTK_INVALID_LIST_POSITION) {
        return;
    }

    engine_name = control_string_array_get(control->engine_id_model, selected);
    control_activate_engine(control, engine_name);
}
