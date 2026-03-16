#include "control_internal.h"
#include "typio/config_schema.h"

static const char *control_css =
    "window.control-root,\n"
    ".window-shell,\n"
    ".control-root {\n"
    "  background-color: alpha(@window_bg_color, 1.0);\n"
    "  color: @window_fg_color;\n"
    "  background-image: none;\n"
    "}\n"
    ".window-shell {\n"
    "  min-height: 0;\n"
    "}\n"
    ".nav-shell {\n"
    "  background-color: alpha(@headerbar_bg_color, 1.0);\n"
    "}\n"
    ".content-shell { background-color: alpha(@window_bg_color, 1.0); }\n"
    ".nav-sidebar { padding: 8px 0; min-height: 0; }\n"
    ".nav-sidebar row {\n"
    "  min-height: 38px;\n"
    "  margin: 2px 8px;\n"
    "  border-radius: 10px;\n"
    "}\n"
    ".status-banner {\n"
    "  background-color: alpha(@view_bg_color, 1.0);\n"
    "  border-radius: 10px;\n"
    "  padding: 10px 12px;\n"
    "  font-weight: 600;\n"
    "}\n"
    ".inline-status {\n"
    "  opacity: 0.82;\n"
    "  font-weight: 500;\n"
    "}\n"
    ".section-title {\n"
    "  font-weight: 700;\n"
    "  opacity: 0.95;\n"
    "}\n"
    ".field-label {\n"
    "  font-weight: 600;\n"
    "  opacity: 0.85;\n"
    "}\n"
    ".empty-note {\n"
    "  opacity: 0.72;\n"
    "}\n"
    ".surface {\n"
    "  background-color: alpha(@card_bg_color, 1.0);\n"
    "  border-radius: 12px;\n"
    "  padding: 16px;\n"
    "}\n"
    ".surface entry,\n"
    ".surface dropdown,\n"
    ".surface spinbutton,\n"
    ".surface button {\n"
    "  min-height: 36px;\n"
    "}\n"
    ".compact-switcher {\n"
    "  margin-bottom: 8px;\n"
    "}\n"
    ".compact-switcher button {\n"
    "  min-height: 34px;\n"
    "  padding: 0 14px;\n"
    "}\n";

static GtkWidget *create_section_title(const char *text) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "section-title");
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 4);
    return label;
}

static GtkWidget *create_field_label(const char *text) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_size_request(label, 120, -1);
    gtk_widget_add_css_class(label, "field-label");
    return label;
}

static GtkWidget *create_field_hint(const char *text) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "empty-note");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    return label;
}

static GtkWidget *create_empty_note(const char *text) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "empty-note");
    return label;
}

static GtkWidget *create_surface_box(gint spacing) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
    gtk_widget_add_css_class(box, "surface");
    return box;
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

static GtkWidget *build_rime_config(TypioControl *control) {
    GtkWidget *grid = gtk_grid_new();
    const TypioConfigField *f;

    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    f = typio_config_schema_find("engines.rime.schema");
    gtk_grid_attach(GTK_GRID(grid), create_field_label(f ? f->ui_label : "Schema"), 0, 0, 1, 1);
    control->rime_schema_model = gtk_string_list_new(nullptr);
    control->rime_schema_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->rime_schema_model), nullptr));
    gtk_widget_set_hexpand(GTK_WIDGET(control->rime_schema_dropdown), TRUE);
    g_signal_connect(control->rime_schema_dropdown, "notify::selected",
                     G_CALLBACK(on_display_dropdown_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->rime_schema_dropdown), 1, 0, 1, 1);

    f = typio_config_schema_find("engines.rime.page_size");
    gtk_grid_attach(GTK_GRID(grid), create_field_label(f ? f->ui_label : "Page size"), 0, 1, 1, 1);
    {
        GtkAdjustment *adj = gtk_adjustment_new(
            f ? f->def.i : 9, f ? f->ui_min : 1, f ? f->ui_max : 20,
            f ? f->ui_step : 1, f ? f->ui_step : 1, 0.0);
        control->rime_page_size_spin = GTK_SPIN_BUTTON(gtk_spin_button_new(adj, 1.0, 0));
    }
    g_signal_connect(control->rime_page_size_spin, "value-changed",
                     G_CALLBACK(on_form_spin_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->rime_page_size_spin), 1, 1, 1, 1);
    control_register_binding(control, "engines.rime.page_size",
                             GTK_WIDGET(control->rime_page_size_spin));

    return grid;
}

static GtkWidget *build_mozc_config(TypioControl *control) {
    GtkWidget *grid = gtk_grid_new();
    const TypioConfigField *f;

    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    f = typio_config_schema_find("engines.mozc.page_size");
    gtk_grid_attach(GTK_GRID(grid), create_field_label(f ? f->ui_label : "Page size"), 0, 0, 1, 1);
    {
        GtkAdjustment *adj = gtk_adjustment_new(
            f ? f->def.i : 9, f ? f->ui_min : 1, f ? f->ui_max : 20,
            f ? f->ui_step : 1, f ? f->ui_step : 1, 0.0);
        control->mozc_page_size_spin = GTK_SPIN_BUTTON(gtk_spin_button_new(adj, 1.0, 0));
    }
    g_signal_connect(control->mozc_page_size_spin, "value-changed",
                     G_CALLBACK(on_form_spin_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->mozc_page_size_spin), 1, 0, 1, 1);
    control_register_binding(control, "engines.mozc.page_size",
                             GTK_WIDGET(control->mozc_page_size_spin));

    return grid;
}

static GtkWidget *build_keyboard_page(TypioControl *control) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *surface;
    GtkWidget *row;
    GtkWidget *stack_box;

    gtk_widget_set_margin_top(page, 16);
    gtk_widget_set_margin_bottom(page, 16);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);

    surface = create_surface_box(12);
    gtk_box_append(GTK_BOX(page), surface);

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(surface), row);
    gtk_box_append(GTK_BOX(row), create_field_label("Keyboard Engine"));

    control->engine_model = gtk_string_list_new(nullptr);
    control->engine_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->engine_model), nullptr));
    gtk_widget_set_hexpand(GTK_WIDGET(control->engine_dropdown), TRUE);
    g_signal_connect(control->engine_dropdown, "notify::selected",
                     G_CALLBACK(on_engine_selected), control);
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(control->engine_dropdown));

    control->engine_config_title = create_section_title("Engine Configuration");
    gtk_box_append(GTK_BOX(surface), control->engine_config_title);

    control->engine_config_stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(control->engine_config_stack,
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    stack_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(stack_box), GTK_WIDGET(control->engine_config_stack));
    gtk_box_append(GTK_BOX(surface), stack_box);

    gtk_stack_add_named(control->engine_config_stack,
                        create_empty_note("This engine has no settings."),
                        "empty");
    gtk_stack_add_named(control->engine_config_stack,
                        create_empty_note("This engine has no settings."),
                        "basic");
    gtk_stack_add_named(control->engine_config_stack, build_rime_config(control), "rime");
    gtk_stack_add_named(control->engine_config_stack, build_mozc_config(control), "mozc");
    gtk_stack_set_visible_child_name(control->engine_config_stack, "empty");
    gtk_widget_set_visible(control->engine_config_title, FALSE);

    control->state_buffer = gtk_text_buffer_new(nullptr);
    return page;
}

static GtkWidget *build_voice_page(TypioControl *control) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *surface;
    GtkWidget *grid;
    GtkWidget *scroller;

    gtk_widget_set_margin_top(page, 16);
    gtk_widget_set_margin_bottom(page, 16);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);

    surface = create_surface_box(12);
    gtk_box_append(GTK_BOX(page), surface);
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_append(GTK_BOX(surface), grid);

    gtk_grid_attach(GTK_GRID(grid), create_field_label("Voice Backend"), 0, 0, 1, 1);
    control->voice_backend_model = gtk_string_list_new(nullptr);
    control->voice_backend_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->voice_backend_model), nullptr));
    gtk_widget_set_hexpand(GTK_WIDGET(control->voice_backend_dropdown), TRUE);
    g_signal_connect(control->voice_backend_dropdown, "notify::selected",
                     G_CALLBACK(on_voice_backend_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->voice_backend_dropdown), 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), create_field_label("Voice Model"), 0, 1, 1, 1);
    control->voice_model_list = gtk_string_list_new(nullptr);
    control->voice_model_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->voice_model_list), nullptr));
    g_signal_connect(control->voice_model_dropdown, "notify::selected",
                     G_CALLBACK(on_display_dropdown_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->voice_model_dropdown), 1, 1, 1, 1);

    control->sherpa_models_frame = control_build_sherpa_model_section(control);
    gtk_box_append(GTK_BOX(page), control->sherpa_models_frame);
    control->whisper_models_frame = control_build_whisper_model_section(control);
    gtk_box_append(GTK_BOX(page), control->whisper_models_frame);
    voice_update_model_sections(control);

    scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), page);
    gtk_widget_set_vexpand(scroller, TRUE);
    return scroller;
}

static const char *gdk_modifier_name(guint mod_bit) {
    if (mod_bit & GDK_CONTROL_MASK) return "Ctrl";
    if (mod_bit & GDK_SHIFT_MASK) return "Shift";
    if (mod_bit & GDK_ALT_MASK) return "Alt";
    if (mod_bit & GDK_SUPER_MASK) return "Super";
    return NULL;
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

    gboolean key_is_modifier = (keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R ||
                                keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R ||
                                keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R ||
                                keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R ||
                                keyval == GDK_KEY_Meta_L || keyval == GDK_KEY_Meta_R);

    if (!key_is_modifier && keyval != 0) {
        const char *name;

        if (str->len > 0) {
            g_string_append_c(str, '+');
        }
        name = gdk_keyval_name(gdk_keyval_to_lower(keyval));
        if (name) {
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
    control_stage_form_change(control);
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
    control_stage_form_change(control);
}

GtkWidget *control_build_shortcuts_page(TypioControl *control) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *surface;
    GtkWidget *grid;
    GtkEventController *key_ec;

    gtk_widget_set_margin_top(page, 16);
    gtk_widget_set_margin_bottom(page, 16);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);

    surface = create_surface_box(12);
    gtk_box_append(GTK_BOX(page), surface);
    gtk_box_append(GTK_BOX(surface), create_section_title("Keyboard Shortcuts"));

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_append(GTK_BOX(surface), grid);

    gtk_grid_attach(GTK_GRID(grid), create_field_label("Switch engine"), 0, 0, 1, 1);
    control->shortcut_switch_engine_btn = GTK_BUTTON(gtk_button_new_with_label("Ctrl+Shift"));
    gtk_widget_set_hexpand(GTK_WIDGET(control->shortcut_switch_engine_btn), TRUE);
    g_signal_connect(control->shortcut_switch_engine_btn, "clicked",
                     G_CALLBACK(shortcut_btn_clicked), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->shortcut_switch_engine_btn), 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), create_field_label("Voice (PTT)"), 0, 1, 1, 1);
    control->shortcut_voice_ptt_btn = GTK_BUTTON(gtk_button_new_with_label("Super+v"));
    g_signal_connect(control->shortcut_voice_ptt_btn, "clicked",
                     G_CALLBACK(shortcut_btn_clicked), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->shortcut_voice_ptt_btn), 1, 1, 1, 1);

    key_ec = gtk_event_controller_key_new();
    g_signal_connect(key_ec, "key-pressed",
                     G_CALLBACK(shortcut_key_pressed), control);
    g_signal_connect(key_ec, "key-released",
                     G_CALLBACK(shortcut_key_released), control);
    gtk_widget_add_controller(page, key_ec);

    return page;
}

GtkWidget *control_build_display_page(TypioControl *control) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *surface;
    GtkWidget *grid;
    GtkWidget *notifications_surface;
    GtkWidget *notifications_grid;
    const TypioConfigField *f;

    gtk_widget_set_margin_top(page, 16);
    gtk_widget_set_margin_bottom(page, 16);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);

    surface = create_surface_box(12);
    gtk_box_append(GTK_BOX(page), surface);
    gtk_box_append(GTK_BOX(surface), create_section_title("Candidate Popup"));

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_append(GTK_BOX(surface), grid);

    f = typio_config_schema_find("engines.rime.popup_theme");
    gtk_grid_attach(GTK_GRID(grid), create_field_label(f ? f->ui_label : "Theme"), 0, 0, 1, 1);
    control->popup_theme_model = gtk_string_list_new(f ? f->ui_options : NULL);
    control->popup_theme_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->popup_theme_model), nullptr));
    gtk_widget_set_hexpand(GTK_WIDGET(control->popup_theme_dropdown), TRUE);
    g_signal_connect(control->popup_theme_dropdown, "notify::selected",
                     G_CALLBACK(on_display_dropdown_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->popup_theme_dropdown), 1, 0, 1, 1);
    control_register_binding(control, "engines.rime.popup_theme",
                             GTK_WIDGET(control->popup_theme_dropdown));

    f = typio_config_schema_find("engines.rime.candidate_layout");
    gtk_grid_attach(GTK_GRID(grid), create_field_label(f ? f->ui_label : "Layout"), 0, 1, 1, 1);
    control->candidate_layout_model = gtk_string_list_new(f ? f->ui_options : NULL);
    control->candidate_layout_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->candidate_layout_model), nullptr));
    g_signal_connect(control->candidate_layout_dropdown, "notify::selected",
                     G_CALLBACK(on_display_dropdown_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->candidate_layout_dropdown), 1, 1, 1, 1);
    control_register_binding(control, "engines.rime.candidate_layout",
                             GTK_WIDGET(control->candidate_layout_dropdown));

    f = typio_config_schema_find("engines.rime.font_size");
    gtk_grid_attach(GTK_GRID(grid), create_field_label(f ? f->ui_label : "Font size"), 0, 2, 1, 1);
    {
        GtkAdjustment *adj = gtk_adjustment_new(
            f ? f->def.i : 11, f ? f->ui_min : 6, f ? f->ui_max : 72,
            f ? f->ui_step : 1, f ? f->ui_step : 1, 0.0);
        control->font_size_spin = GTK_SPIN_BUTTON(gtk_spin_button_new(adj, 1.0, 0));
    }
    g_signal_connect(control->font_size_spin, "value-changed",
                     G_CALLBACK(on_display_spin_changed), control);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(control->font_size_spin), 1, 2, 1, 1);
    control_register_binding(control, "engines.rime.font_size",
                             GTK_WIDGET(control->font_size_spin));

    notifications_surface = create_surface_box(12);
    gtk_box_append(GTK_BOX(page), notifications_surface);
    gtk_box_append(GTK_BOX(notifications_surface), create_section_title("Notifications"));

    notifications_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(notifications_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(notifications_grid), 12);
    gtk_box_append(GTK_BOX(notifications_surface), notifications_grid);

    f = typio_config_schema_find("notifications.enable");
    gtk_grid_attach(GTK_GRID(notifications_grid), create_field_label(f ? f->ui_label : "Enable"), 0, 0, 1, 1);
    control->notifications_enable_switch = GTK_SWITCH(gtk_switch_new());
    g_signal_connect(control->notifications_enable_switch, "notify::active",
                     G_CALLBACK(on_display_switch_changed), control);
    gtk_grid_attach(GTK_GRID(notifications_grid),
                    GTK_WIDGET(control->notifications_enable_switch), 1, 0, 1, 1);
    control_register_binding(control, "notifications.enable",
                             GTK_WIDGET(control->notifications_enable_switch));

    f = typio_config_schema_find("notifications.startup_checks");
    gtk_grid_attach(GTK_GRID(notifications_grid), create_field_label(f ? f->ui_label : "Startup checks"), 0, 1, 1, 1);
    control->notifications_startup_switch = GTK_SWITCH(gtk_switch_new());
    g_signal_connect(control->notifications_startup_switch, "notify::active",
                     G_CALLBACK(on_display_switch_changed), control);
    gtk_grid_attach(GTK_GRID(notifications_grid),
                    GTK_WIDGET(control->notifications_startup_switch), 1, 1, 1, 1);
    control_register_binding(control, "notifications.startup_checks",
                             GTK_WIDGET(control->notifications_startup_switch));

    f = typio_config_schema_find("notifications.runtime");
    gtk_grid_attach(GTK_GRID(notifications_grid), create_field_label(f ? f->ui_label : "Runtime alerts"), 0, 2, 1, 1);
    control->notifications_runtime_switch = GTK_SWITCH(gtk_switch_new());
    g_signal_connect(control->notifications_runtime_switch, "notify::active",
                     G_CALLBACK(on_display_switch_changed), control);
    gtk_grid_attach(GTK_GRID(notifications_grid),
                    GTK_WIDGET(control->notifications_runtime_switch), 1, 2, 1, 1);
    control_register_binding(control, "notifications.runtime",
                             GTK_WIDGET(control->notifications_runtime_switch));

    f = typio_config_schema_find("notifications.voice");
    gtk_grid_attach(GTK_GRID(notifications_grid), create_field_label(f ? f->ui_label : "Voice alerts"), 0, 3, 1, 1);
    control->notifications_voice_switch = GTK_SWITCH(gtk_switch_new());
    g_signal_connect(control->notifications_voice_switch, "notify::active",
                     G_CALLBACK(on_display_switch_changed), control);
    gtk_grid_attach(GTK_GRID(notifications_grid),
                    GTK_WIDGET(control->notifications_voice_switch), 1, 3, 1, 1);
    control_register_binding(control, "notifications.voice",
                             GTK_WIDGET(control->notifications_voice_switch));

    f = typio_config_schema_find("notifications.cooldown_ms");
    gtk_grid_attach(GTK_GRID(notifications_grid), create_field_label(f ? f->ui_label : "Cooldown (ms)"), 0, 4, 1, 1);
    {
        GtkAdjustment *adj = gtk_adjustment_new(
            f ? f->def.i : 15000, f ? f->ui_min : 0, f ? f->ui_max : 300000,
            f ? f->ui_step : 1000, 5000.0, 0.0);
        control->notifications_cooldown_spin =
            GTK_SPIN_BUTTON(gtk_spin_button_new(adj, 1000.0, 0));
    }
    g_signal_connect(control->notifications_cooldown_spin, "value-changed",
                     G_CALLBACK(on_display_spin_changed), control);
    gtk_grid_attach(GTK_GRID(notifications_grid),
                    GTK_WIDGET(control->notifications_cooldown_spin), 1, 4, 1, 1);
    control_register_binding(control, "notifications.cooldown_ms",
                             GTK_WIDGET(control->notifications_cooldown_spin));

    gtk_box_append(GTK_BOX(notifications_surface),
                   create_field_hint("Runtime alerts cover configuration reload, Wayland capability, and voice availability warnings."));

    return page;
}

GtkWidget *control_wrap_page_scroller(GtkWidget *child) {
    GtkWidget *scroller = gtk_scrolled_window_new();

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), child);
    gtk_widget_set_vexpand(scroller, TRUE);
    return scroller;
}

GtkWidget *control_build_engines_page(TypioControl *control) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *switcher = gtk_stack_switcher_new();
    GtkWidget *stack = gtk_stack_new();

    gtk_widget_set_margin_top(page, 12);
    gtk_widget_set_margin_bottom(page, 12);
    gtk_widget_set_margin_start(page, 12);
    gtk_widget_set_margin_end(page, 12);

    gtk_widget_add_css_class(switcher, "compact-switcher");
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
    gtk_box_append(GTK_BOX(page), switcher);

    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_add_titled(GTK_STACK(stack),
                         control_wrap_page_scroller(build_keyboard_page(control)),
                         "keyboard", "Keyboard");
    gtk_stack_add_titled(GTK_STACK(stack), build_voice_page(control), "voice", "Voice");
    gtk_box_append(GTK_BOX(page), stack);

    return page;
}

void control_apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();

    gtk_css_provider_load_from_string(provider, control_css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}
