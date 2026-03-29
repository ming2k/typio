#include "control_internal.h"
#include "control_widgets.h"
#include "typio/config_schema.h"

#include <gdk/gdkkeysyms.h>

static void connect_binding_change_signal(GtkWidget *widget, gpointer user_data) {
    if (GTK_IS_SWITCH(widget)) {
        g_signal_connect(widget, "notify::active",
                         G_CALLBACK(on_display_switch_changed), user_data);
    } else if (GTK_IS_DROP_DOWN(widget)) {
        g_signal_connect(widget, "notify::selected",
                         G_CALLBACK(on_display_dropdown_changed), user_data);
    } else if (GTK_IS_SPIN_BUTTON(widget)) {
        g_signal_connect(widget, "value-changed",
                         G_CALLBACK(on_display_spin_changed), user_data);
    } else if (GTK_IS_EDITABLE(widget)) {
        g_signal_connect(widget, "changed",
                         G_CALLBACK(on_display_entry_changed), user_data);
    }
}

static void control_register_binding(TypioControl *control,
                                     const char *key,
                                     GtkWidget *widget) {
    const TypioConfigField *field = typio_config_schema_find(key);

    if (!field || control->binding_count >= G_N_ELEMENTS(control->bindings)) {
        return;
    }

    control->bindings[control->binding_count].field = field;
    control->bindings[control->binding_count].widget = widget;
    control->binding_count++;
}

static void control_init_state_binding(ControlStateBinding *binding,
                                       const char *config_key,
                                       GtkDropDown *dropdown,
                                       gpointer user_data,
                                       ControlStateIndexFunc find_index,
                                       ControlStateValueFunc get_value,
                                       ControlStateValueSource source) {
    if (!binding) {
        return;
    }

    binding->config_key = config_key;
    binding->dropdown = dropdown;
    binding->user_data = user_data;
    binding->find_index = find_index;
    binding->get_value = get_value;
    binding->source = source;
    binding->options_user_data = NULL;
    binding->refresh_options = NULL;
}

static guint control_string_list_index(gpointer user_data, const char *value) {
    GtkStringList *model = user_data;
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

static const char *control_string_list_value(gpointer user_data, guint index) {
    GtkStringList *model = user_data;

    if (!model || index == GTK_INVALID_LIST_POSITION) {
        return NULL;
    }

    return gtk_string_list_get_string(model, index);
}

static guint control_string_array_index(gpointer user_data, const char *value) {
    GPtrArray *values = user_data;

    if (!values || !value) {
        return GTK_INVALID_LIST_POSITION;
    }

    for (guint i = 0; i < values->len; ++i) {
        const char *item = g_ptr_array_index(values, i);
        if (item && g_strcmp0(item, value) == 0) {
            return i;
        }
    }

    return GTK_INVALID_LIST_POSITION;
}

static const char *control_string_array_value(gpointer user_data, guint index) {
    GPtrArray *values = user_data;

    if (!values || index == GTK_INVALID_LIST_POSITION || index >= values->len) {
        return NULL;
    }

    return g_ptr_array_index(values, index);
}

static guint control_rime_schema_index(gpointer user_data, const char *value) {
    return control_string_array_index(user_data, value ? value : "");
}

static const char *control_rime_schema_value(gpointer user_data, guint index) {
    const char *value = control_string_array_value(user_data, index);

    if (!value || !*value) {
        return NULL;
    }

    return value;
}

static GtkWidget *create_bound_widget(TypioControl *control,
                                      const char *key) {
    const TypioConfigField *field = typio_config_schema_find(key);
    GtkWidget *widget = control_binding_create_widget(field);
    char *name;

    if (!widget) {
        GtkWidget *fallback = gtk_label_new("Unavailable");
        control_name_widget(fallback, "field-unavailable");
        return fallback;
    }

    name = control_build_debug_name("field", key);
    control_name_widget(widget, name);
    g_free(name);
    connect_binding_change_signal(widget, control);
    control_register_binding(control, key, widget);
    return widget;
}

static GtkWidget *build_rime_config(TypioControl *control) {
    GtkWidget *box = control_create_panel_box_named("rime-config-section", 12);
    GtkWidget *list = control_create_preferences_list_named("rime-config-list");

    gtk_box_append(GTK_BOX(box),
                   control_create_section_header_named("rime-config-header",
                                                       "Rime",
                                                       "Optional Chinese input settings and schema tuning."));

    control->rime_schema_model = gtk_string_list_new(nullptr);
    control->rime_schema_id_model = g_ptr_array_new_with_free_func(g_free);
    control->rime_schema_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->rime_schema_model), nullptr));
    control_name_widget(GTK_WIDGET(control->rime_schema_dropdown), "rime-schema-dropdown");
    g_signal_connect(control->rime_schema_dropdown, "notify::selected",
                     G_CALLBACK(on_display_dropdown_changed), control);
    control_init_state_binding(&control->rime_schema_state,
                               "engines.rime.schema",
                               control->rime_schema_dropdown,
                               control->rime_schema_id_model,
                               control_rime_schema_index,
                               control_rime_schema_value,
                               CONTROL_STATE_VALUE_FROM_RUNTIME);
    control->rime_schema_state.options_user_data = control;
    control->rime_schema_state.refresh_options = control_refresh_rime_schema_options;
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row_named("rime-schema-row",
                                                            "Schema",
                                                            "",
                                                            GTK_WIDGET(control->rime_schema_dropdown)));

    gtk_box_append(GTK_BOX(box), list);
    return box;
}

static GtkWidget *build_mozc_config(TypioControl *control) {
    GtkWidget *box = control_create_panel_box_named("mozc-config-section", 12);

    gtk_box_append(GTK_BOX(box),
                   control_create_section_header_named("mozc-config-header",
                                                       "Mozc",
                                                       "Mozc manages candidate paging through its own configuration."));
    return box;
}

static GtkWidget *build_keyboard_section(TypioControl *control) {
    GtkWidget *box = control_create_panel_box_named("keyboard-section", 14);
    GtkWidget *order_box;
    GtkWidget *order_editor;
    GtkWidget *order_mode_box;
    GtkWidget *order_add_box;
    GtkWidget *order_add_button;
    GtkWidget *order_reset_button;
    GtkWidget *settings_headerbar;
    GtkWidget *settings_root;
    GtkWidget *settings_shell;

    gtk_box_append(GTK_BOX(box),
                   control_create_section_header_named("keyboard-header",
                                                       "Keyboard engines",
                                                       "Keyboard input is the primary engine category. Exactly one keyboard engine is active at a time, and it owns composition, candidates, and commit behavior."));

    control->engine_model = gtk_string_list_new(nullptr);
    control->engine_id_model = g_ptr_array_new_with_free_func(g_free);
    control->engine_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->engine_model), nullptr));
    control_name_widget(GTK_WIDGET(control->engine_dropdown), "keyboard-engine-dropdown");
    g_signal_connect(control->engine_dropdown, "notify::selected",
                     G_CALLBACK(on_engine_selected), control);
    control_init_state_binding(&control->keyboard_engine_state,
                               "default_engine",
                               control->engine_dropdown,
                               control->engine_id_model,
                               control_string_array_index,
                               control_string_array_value,
                               CONTROL_STATE_VALUE_FROM_RUNTIME);
    gtk_widget_set_visible(GTK_WIDGET(control->engine_dropdown), FALSE);

    order_box = control_create_panel_box_named("keyboard-overview-page", 10);

    order_mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    control_name_widget(order_mode_box, "keyboard-order-mode-box");
    control->engine_order_mode_label = GTK_LABEL(gtk_label_new(""));
    control_name_widget(GTK_WIDGET(control->engine_order_mode_label), "keyboard-order-mode-label");
    gtk_label_set_xalign(control->engine_order_mode_label, 0.0f);
    gtk_widget_add_css_class(GTK_WIDGET(control->engine_order_mode_label), "preference-description");
    gtk_box_append(GTK_BOX(order_mode_box), GTK_WIDGET(control->engine_order_mode_label));

    gtk_box_append(GTK_BOX(order_box), order_mode_box);

    control->engine_order_list = GTK_LIST_BOX(control_create_preferences_list_named("keyboard-order-list"));
    gtk_widget_add_css_class(GTK_WIDGET(control->engine_order_list), "engine-order-list");
    gtk_box_append(GTK_BOX(order_box), GTK_WIDGET(control->engine_order_list));

    order_editor = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    control_name_widget(order_editor, "keyboard-order-editor");
    control->engine_order_add_model = gtk_string_list_new(NULL);
    control->engine_order_add_id_model = g_ptr_array_new_with_free_func(g_free);
    control->engine_order_add_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->engine_order_add_model), NULL));
    control_name_widget(GTK_WIDGET(control->engine_order_add_dropdown), "keyboard-order-add-dropdown");
    order_add_button = gtk_button_new_with_label("Add to order");
    control_name_widget(order_add_button, "keyboard-order-add-button");
    gtk_widget_add_css_class(order_add_button, "control-button");
    g_signal_connect(order_add_button, "clicked",
                     G_CALLBACK(on_engine_order_add_clicked), control);
    order_reset_button = gtk_button_new_with_label("Reset");
    control->engine_order_reset_button = GTK_BUTTON(order_reset_button);
    control_name_widget(order_reset_button, "keyboard-order-reset-button");
    gtk_widget_add_css_class(order_reset_button, "control-button");
    g_signal_connect(order_reset_button, "clicked",
                     G_CALLBACK(on_engine_order_reset_clicked), control);

    order_add_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    control_name_widget(order_add_box, "keyboard-order-add-box");
    gtk_box_append(GTK_BOX(order_add_box), GTK_WIDGET(control->engine_order_add_dropdown));
    gtk_box_append(GTK_BOX(order_add_box), order_add_button);
    gtk_box_append(GTK_BOX(order_add_box), order_reset_button);
    gtk_box_append(GTK_BOX(order_editor), order_add_box);
    gtk_box_append(GTK_BOX(order_editor),
                   control_create_empty_note_named(
                       "keyboard-order-note",
                       "Drag to reorder. Use the pencil button to open settings for a specific engine."));
    gtk_box_append(GTK_BOX(order_box), order_editor);
    gtk_box_append(GTK_BOX(box), order_box);

    control->engine_settings_window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(control->engine_settings_window, "Engine settings");
    gtk_window_set_default_size(control->engine_settings_window, 520, 420);
    gtk_window_set_modal(control->engine_settings_window, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(control->engine_settings_window), "control-root");
    control_name_widget(GTK_WIDGET(control->engine_settings_window), "engine-settings-window");
    settings_headerbar = gtk_header_bar_new();
    control_name_widget(settings_headerbar, "engine-settings-headerbar");
    gtk_widget_add_css_class(settings_headerbar, "control-headerbar");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(settings_headerbar), TRUE);
    gtk_window_set_titlebar(control->engine_settings_window, settings_headerbar);
    if (control->window) {
        gtk_window_set_transient_for(control->engine_settings_window,
                                     GTK_WINDOW(control->window));
    }
    g_signal_connect(control->engine_settings_window, "close-request",
                     G_CALLBACK(on_engine_settings_window_close_request), control);

    settings_root = control_create_page_shell_named("engine-settings-window-root");
    gtk_widget_add_css_class(settings_root, "control-shell");
    settings_shell = control_create_panel_box_named("engine-settings-window-shell", 12);
    gtk_box_append(GTK_BOX(settings_root), settings_shell);
    gtk_window_set_child(control->engine_settings_window, settings_root);

    control->engine_config_stack = GTK_STACK(gtk_stack_new());
    control_name_widget(GTK_WIDGET(control->engine_config_stack), "engine-config-stack");
    gtk_widget_add_css_class(GTK_WIDGET(control->engine_config_stack), "engine-config");
    gtk_stack_set_transition_type(control->engine_config_stack,
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(control->engine_config_stack,
                        control_create_empty_note_named("engine-config-empty-note",
                                                        "This engine has no configurable options."),
                        "empty");
    gtk_stack_add_named(control->engine_config_stack,
                        control_create_empty_note_named("engine-config-basic-note",
                                                        "The built-in basic engine has no extra settings."),
                        "basic");
    gtk_stack_add_named(control->engine_config_stack, build_rime_config(control), "rime");
    gtk_stack_add_named(control->engine_config_stack, build_mozc_config(control), "mozc");
    gtk_stack_set_visible_child_name(control->engine_config_stack, "empty");
    gtk_box_append(GTK_BOX(settings_shell), GTK_WIDGET(control->engine_config_stack));

    control->engine_order_model = gtk_string_list_new(NULL);
    control_refresh_engine_order_editor(control);

    return box;
}

static GtkWidget *build_voice_section(TypioControl *control) {
    GtkWidget *box = control_create_panel_box_named("voice-section", 14);
    GtkWidget *list = control_create_preferences_list_named("voice-list");

    gtk_box_append(GTK_BOX(box),
                   control_create_section_header_named("voice-header",
                                                       "Voice input",
                                                       "Voice backends run alongside keyboard input. They do not replace the active keyboard engine, but only one voice backend is active at a time."));

    control->voice_backend_model = gtk_string_list_new(nullptr);
    control->voice_backend_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->voice_backend_model), nullptr));
    control_name_widget(GTK_WIDGET(control->voice_backend_dropdown), "voice-backend-dropdown");
    g_signal_connect(control->voice_backend_dropdown, "notify::selected",
                     G_CALLBACK(on_voice_backend_changed), control);
    control_init_state_binding(&control->voice_backend_state,
                               "default_voice_engine",
                               control->voice_backend_dropdown,
                               control->voice_backend_model,
                               control_string_list_index,
                               control_string_list_value,
                               CONTROL_STATE_VALUE_RUNTIME_THEN_CONFIG);
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row_named("voice-backend-row",
                                                            "Voice backend",
                                                            "Switch between Whisper and sherpa-onnx when both are available.",
                                                            GTK_WIDGET(control->voice_backend_dropdown)));

    control->voice_model_list = gtk_string_list_new(nullptr);
    control->voice_model_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->voice_model_list), nullptr));
    control_name_widget(GTK_WIDGET(control->voice_model_dropdown), "voice-model-dropdown");
    g_signal_connect(control->voice_model_dropdown, "notify::selected",
                     G_CALLBACK(on_display_dropdown_changed), control);
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row_named("voice-model-row",
                                                            "Installed model",
                                                            "Choose the local voice model used by the selected backend.",
                                                            GTK_WIDGET(control->voice_model_dropdown)));

    gtk_box_append(GTK_BOX(box), list);

    control->whisper_models_frame = control_build_whisper_model_section(control);
    control->sherpa_models_frame = control_build_sherpa_model_section(control);
    gtk_box_append(GTK_BOX(box), control->whisper_models_frame);
    gtk_box_append(GTK_BOX(box), control->sherpa_models_frame);

    return box;
}

static const char *gdk_modifier_name(guint mod_bit) {
    switch (mod_bit) {
    case GDK_CONTROL_MASK:
        return "Ctrl";
    case GDK_ALT_MASK:
        return "Alt";
    case GDK_SUPER_MASK:
        return "Super";
    case GDK_SHIFT_MASK:
        return "Shift";
    default:
        return "";
    }
}

static char *format_gdk_shortcut(guint keyval, GdkModifierType state) {
    GString *str = g_string_new(NULL);
    static const guint mod_order[] = {
        GDK_CONTROL_MASK, GDK_ALT_MASK, GDK_SUPER_MASK, GDK_SHIFT_MASK
    };

    for (size_t i = 0; i < G_N_ELEMENTS(mod_order); i++) {
        if (state & mod_order[i]) {
            if (str->len > 0) {
                g_string_append_c(str, '+');
            }
            g_string_append(str, gdk_modifier_name(mod_order[i]));
        }
    }

    if (keyval != 0) {
        const char *name = gdk_keyval_name(gdk_keyval_to_lower(keyval));

        if (name) {
            if (str->len > 0) {
                g_string_append_c(str, '+');
            }
            g_string_append(str, name);
        }
    }

    return g_string_free(str, FALSE);
}

static void shortcut_btn_clicked([[maybe_unused]] GtkButton *button, gpointer user_data) {
    TypioControl *control = user_data;

    if (control->shortcut_recording_btn == button) {
        control->shortcut_recording_btn = NULL;
        control_sync_form_from_buffer(control);
        return;
    }

    if (control->shortcut_recording_btn) {
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
    GdkModifierType relevant;
    char *shortcut;

    if (!control->shortcut_recording_btn) {
        return FALSE;
    }

    if (keyval == GDK_KEY_Escape) {
        control->shortcut_recording_btn = NULL;
        control_sync_form_from_buffer(control);
        return TRUE;
    }

    if (keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R ||
        keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R ||
        keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R ||
        keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R ||
        keyval == GDK_KEY_Meta_L || keyval == GDK_KEY_Meta_R) {
        return TRUE;
    }

    relevant = state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_SUPER_MASK);
    if (relevant == 0) {
        return TRUE;
    }

    shortcut = format_gdk_shortcut(keyval, relevant);
    gtk_button_set_label(control->shortcut_recording_btn, shortcut);
    g_free(shortcut);

    control->shortcut_recording_btn = NULL;
    control_stage_form_change(control, CONTROL_AUTOSAVE_FAST);
    return TRUE;
}

static void shortcut_key_released([[maybe_unused]] GtkEventControllerKey *ec,
                                  guint keyval,
                                  [[maybe_unused]] guint keycode,
                                  GdkModifierType state,
                                  gpointer user_data) {
    TypioControl *control = user_data;
    GdkModifierType relevant;
    guint released_mod = 0;
    guint full_chord;
    guint mod_count = 0;
    char *shortcut;

    if (!control->shortcut_recording_btn) {
        return;
    }

    relevant = state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_SUPER_MASK);

    if (keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R) {
        released_mod = GDK_CONTROL_MASK;
    } else if (keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R) {
        released_mod = GDK_SHIFT_MASK;
    } else if (keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R) {
        released_mod = GDK_ALT_MASK;
    } else if (keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R) {
        released_mod = GDK_SUPER_MASK;
    } else {
        return;
    }

    full_chord = relevant | released_mod;
    if (full_chord & GDK_CONTROL_MASK) mod_count++;
    if (full_chord & GDK_SHIFT_MASK) mod_count++;
    if (full_chord & GDK_ALT_MASK) mod_count++;
    if (full_chord & GDK_SUPER_MASK) mod_count++;

    if (mod_count < 2) {
        return;
    }

    shortcut = format_gdk_shortcut(0, (GdkModifierType)full_chord);
    gtk_button_set_label(control->shortcut_recording_btn, shortcut);
    g_free(shortcut);

    control->shortcut_recording_btn = NULL;
    control_stage_form_change(control, CONTROL_AUTOSAVE_FAST);
}

GtkWidget *control_build_shortcuts_page(TypioControl *control) {
    GtkWidget *page = control_create_page_shell_named("shortcuts-page");
    GtkWidget *box = control_create_panel_box_named("shortcuts-panel", 14);
    GtkWidget *list = control_create_preferences_list_named("shortcuts-list");
    GtkEventController *key_ec;

    gtk_box_append(GTK_BOX(page),
                   control_create_section_header_named("shortcuts-header",
                                                       "Shortcuts",
                                                       "Record combinations directly from the keyboard. Press Esc to cancel."));

    control->shortcut_switch_engine_btn = GTK_BUTTON(
        gtk_button_new_with_label("Ctrl+Shift"));
    control_name_widget(GTK_WIDGET(control->shortcut_switch_engine_btn), "shortcut-switch-engine-button");
    gtk_widget_add_css_class(GTK_WIDGET(control->shortcut_switch_engine_btn), "control-button");
    g_signal_connect(control->shortcut_switch_engine_btn, "clicked",
                     G_CALLBACK(shortcut_btn_clicked), control);
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row_named("shortcut-switch-engine-row",
                                                            "Switch engine",
                                                            "Cycle the active engine without opening the panel.",
                                                            GTK_WIDGET(control->shortcut_switch_engine_btn)));

    control->shortcut_emergency_exit_btn = GTK_BUTTON(
        gtk_button_new_with_label("Ctrl+Shift+Escape"));
    control_name_widget(GTK_WIDGET(control->shortcut_emergency_exit_btn),
                        "shortcut-emergency-exit-button");
    gtk_widget_add_css_class(GTK_WIDGET(control->shortcut_emergency_exit_btn), "control-button");
    g_signal_connect(control->shortcut_emergency_exit_btn, "clicked",
                     G_CALLBACK(shortcut_btn_clicked), control);
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row_named("shortcut-emergency-exit-row",
                                                            "Emergency exit",
                                                            "Immediately stop Typio and release control when input routing misbehaves.",
                                                            GTK_WIDGET(control->shortcut_emergency_exit_btn)));

    control->shortcut_voice_ptt_btn = GTK_BUTTON(
        gtk_button_new_with_label("Super+v"));
    control_name_widget(GTK_WIDGET(control->shortcut_voice_ptt_btn), "shortcut-voice-ptt-button");
    gtk_widget_add_css_class(GTK_WIDGET(control->shortcut_voice_ptt_btn), "control-button");
    g_signal_connect(control->shortcut_voice_ptt_btn, "clicked",
                     G_CALLBACK(shortcut_btn_clicked), control);
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row_named("shortcut-voice-ptt-row",
                                                            "Voice push-to-talk",
                                                            "Hold the shortcut to activate the configured voice backend.",
                                                            GTK_WIDGET(control->shortcut_voice_ptt_btn)));

    gtk_box_append(GTK_BOX(box), list);
    gtk_box_append(GTK_BOX(box),
                   control_create_empty_note_named("shortcuts-note",
                                                   "Click a shortcut button, then press the new key combination."));
    gtk_box_append(GTK_BOX(page), box);

    key_ec = gtk_event_controller_key_new();
    g_signal_connect(key_ec, "key-pressed", G_CALLBACK(shortcut_key_pressed), control);
    g_signal_connect(key_ec, "key-released", G_CALLBACK(shortcut_key_released), control);
    gtk_widget_add_controller(page, key_ec);
    return page;
}

GtkWidget *control_build_display_page(TypioControl *control) {
    GtkWidget *page = control_create_page_shell_named("display-page");
    GtkWidget *appearance_box = control_create_panel_box_named("display-appearance-section", 14);
    GtkWidget *appearance_list = control_create_preferences_list_named("display-appearance-list");
    GtkWidget *notifications_box = control_create_panel_box_named("display-notifications-section", 14);
    GtkWidget *notifications_list = control_create_preferences_list_named("display-notifications-list");

    gtk_box_append(GTK_BOX(page),
                   control_create_section_header_named("display-appearance-header",
                                                       "Appearance",
                                                       "Tune the popup layout and panel notifications without leaving the session."));

    control->popup_theme_dropdown = GTK_DROP_DOWN(
        create_bound_widget(control, "display.popup_theme"));
    gtk_list_box_append(GTK_LIST_BOX(appearance_list),
                        control_create_preference_row_named("popup-theme-row",
                                                            "Popup theme",
                                                            "Choose whether the candidate popup follows the desktop theme or a fixed appearance.",
                                                            GTK_WIDGET(control->popup_theme_dropdown)));

    control->candidate_layout_dropdown = GTK_DROP_DOWN(
        create_bound_widget(control, "display.candidate_layout"));
    gtk_list_box_append(GTK_LIST_BOX(appearance_list),
                        control_create_preference_row_named("candidate-layout-row",
                                                            "Candidate layout",
                                                            "Select horizontal or vertical candidate arrangement for popup rendering.",
                                                            GTK_WIDGET(control->candidate_layout_dropdown)));

    control->font_size_spin = GTK_SPIN_BUTTON(
        create_bound_widget(control, "display.font_size"));
    gtk_list_box_append(GTK_LIST_BOX(appearance_list),
                        control_create_preference_row_named("font-size-row",
                                                            "Font size",
                                                            "Adjust the popup text size for candidate and preedit content.",
                                                            GTK_WIDGET(control->font_size_spin)));

    control->popup_mode_indicator_switch = GTK_SWITCH(
        create_bound_widget(control, "display.popup_mode_indicator"));
    gtk_list_box_append(GTK_LIST_BOX(appearance_list),
                        control_create_preference_row_named("popup-mode-indicator-row",
                                                            "Mode indicator",
                                                            "Show or hide the engine mode label in the candidate popup.",
                                                            GTK_WIDGET(control->popup_mode_indicator_switch)));

    gtk_box_append(GTK_BOX(appearance_box), appearance_list);
    gtk_box_append(GTK_BOX(page), appearance_box);

    gtk_box_append(GTK_BOX(page),
                   control_create_section_header_named("display-notifications-header",
                                                       "Notifications",
                                                       "Keep runtime alerts useful without turning the panel into a dashboard."));

    control->notifications_enable_switch = GTK_SWITCH(
        create_bound_widget(control, "notifications.enable"));
    gtk_list_box_append(GTK_LIST_BOX(notifications_list),
                        control_create_preference_row_named("notifications-enable-row",
                                                            "Enable notifications",
                                                            "Master switch for panel-managed notification behavior.",
                                                            GTK_WIDGET(control->notifications_enable_switch)));

    control->notifications_startup_switch = GTK_SWITCH(
        create_bound_widget(control, "notifications.startup_checks"));
    gtk_list_box_append(GTK_LIST_BOX(notifications_list),
                        control_create_preference_row_named("notifications-startup-row",
                                                            "Startup checks",
                                                            "Show startup warnings when Typio detects missing runtime prerequisites.",
                                                            GTK_WIDGET(control->notifications_startup_switch)));

    control->notifications_runtime_switch = GTK_SWITCH(
        create_bound_widget(control, "notifications.runtime"));
    gtk_list_box_append(GTK_LIST_BOX(notifications_list),
                        control_create_preference_row_named("notifications-runtime-row",
                                                            "Runtime alerts",
                                                            "Show alerts for service or backend issues during normal operation.",
                                                            GTK_WIDGET(control->notifications_runtime_switch)));

    control->notifications_voice_switch = GTK_SWITCH(
        create_bound_widget(control, "notifications.voice"));
    gtk_list_box_append(GTK_LIST_BOX(notifications_list),
                        control_create_preference_row_named("notifications-voice-row",
                                                            "Voice alerts",
                                                            "Show voice-backend model and microphone related notifications.",
                                                            GTK_WIDGET(control->notifications_voice_switch)));

    control->notifications_cooldown_spin = GTK_SPIN_BUTTON(
        create_bound_widget(control, "notifications.cooldown_ms"));
    gtk_list_box_append(GTK_LIST_BOX(notifications_list),
                        control_create_preference_row_named("notifications-cooldown-row",
                                                            "Cooldown (ms)",
                                                            "Debounce repeated notifications so transient issues do not spam the desktop.",
                                                            GTK_WIDGET(control->notifications_cooldown_spin)));

    gtk_box_append(GTK_BOX(notifications_box), notifications_list);
    gtk_box_append(GTK_BOX(page), notifications_box);
    return page;
}

GtkWidget *control_wrap_page_scroller(GtkWidget *child) {
    GtkWidget *scroller = gtk_scrolled_window_new();
    const char *child_name = gtk_widget_get_name(child);
    char *scroller_name = NULL;

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), child);
    gtk_widget_set_vexpand(scroller, TRUE);
    if (child_name && *child_name) {
        scroller_name = g_strdup_printf("%s-scroller", child_name);
        control_name_widget(scroller, scroller_name);
    }
    g_free(scroller_name);
    return scroller;
}

GtkWidget *control_build_engines_page(TypioControl *control) {
    GtkWidget *page = control_create_page_shell_named("engines-page");

    gtk_box_append(GTK_BOX(page),
                   control_create_section_header_named("engines-header",
                                                       "Input engines",
                                                       "Typio has two parallel engine categories: keyboard for composition and candidates, and voice for speech recognition. Each category keeps its own single active engine."));
    gtk_box_append(GTK_BOX(page), build_keyboard_section(control));
    gtk_box_append(GTK_BOX(page), build_voice_section(control));
    return page;
}
