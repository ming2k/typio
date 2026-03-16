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

static GtkWidget *create_bound_widget(TypioControl *control,
                                      const char *key) {
    const TypioConfigField *field = typio_config_schema_find(key);
    GtkWidget *widget = control_binding_create_widget(field);

    if (!widget) {
        return gtk_label_new("Unavailable");
    }

    connect_binding_change_signal(widget, control);
    control_register_binding(control, key, widget);
    return widget;
}

static GtkWidget *build_rime_config(TypioControl *control) {
    GtkWidget *box = control_create_panel_box(12);
    GtkWidget *list = control_create_preferences_list();
    const TypioConfigField *schema_field = typio_config_schema_find("engines.rime.schema");

    gtk_box_append(GTK_BOX(box),
                   control_create_section_header("Rime", "Optional Chinese input settings and schema tuning."));

    control->rime_schema_model = gtk_string_list_new(nullptr);
    control->rime_schema_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->rime_schema_model), nullptr));
    g_signal_connect(control->rime_schema_dropdown, "notify::selected",
                     G_CALLBACK(on_display_dropdown_changed), control);
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row(schema_field && schema_field->ui_label
                                                  ? schema_field->ui_label
                                                  : "Schema",
                                              "Choose the active schema exposed by the Rime installation.",
                                              GTK_WIDGET(control->rime_schema_dropdown)));

    control->rime_page_size_spin = GTK_SPIN_BUTTON(
        create_bound_widget(control, "engines.rime.page_size"));
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row("Candidate page size",
                                              "Limit how many candidates appear in each popup page.",
                                              GTK_WIDGET(control->rime_page_size_spin)));

    gtk_box_append(GTK_BOX(box), list);
    return box;
}

static GtkWidget *build_mozc_config(TypioControl *control) {
    GtkWidget *box = control_create_panel_box(12);
    GtkWidget *list = control_create_preferences_list();

    gtk_box_append(GTK_BOX(box),
                   control_create_section_header("Mozc", "Japanese candidate paging and engine-specific settings."));

    control->mozc_page_size_spin = GTK_SPIN_BUTTON(
        create_bound_widget(control, "engines.mozc.page_size"));
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row("Candidate page size",
                                              "Limit how many candidates appear in each page.",
                                              GTK_WIDGET(control->mozc_page_size_spin)));

    gtk_box_append(GTK_BOX(box), list);
    return box;
}

static GtkWidget *build_keyboard_section(TypioControl *control) {
    GtkWidget *box = control_create_panel_box(14);
    GtkWidget *list = control_create_preferences_list();

    gtk_box_append(GTK_BOX(box),
                   control_create_section_header("Input engine",
                                         "Switch between the built-in engine and installed plugins."));

    control->engine_model = gtk_string_list_new(nullptr);
    control->engine_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->engine_model), nullptr));
    g_signal_connect(control->engine_dropdown, "notify::selected",
                     G_CALLBACK(on_engine_selected), control);
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row("Keyboard engine",
                                              "Changes the active engine for new input sessions.",
                                              GTK_WIDGET(control->engine_dropdown)));
    gtk_box_append(GTK_BOX(box), list);

    control->engine_config_title = control_create_section_header(
        "Engine settings", "Only engines that expose settings show editable options here.");
    gtk_widget_set_visible(control->engine_config_title, FALSE);
    gtk_box_append(GTK_BOX(box), control->engine_config_title);

    control->engine_config_stack = GTK_STACK(gtk_stack_new());
    gtk_widget_add_css_class(GTK_WIDGET(control->engine_config_stack), "engine-config");
    gtk_stack_set_transition_type(control->engine_config_stack,
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(control->engine_config_stack,
                        control_create_empty_note("This engine has no configurable options."),
                        "empty");
    gtk_stack_add_named(control->engine_config_stack,
                        control_create_empty_note("The built-in basic engine has no extra settings."),
                        "basic");
    gtk_stack_add_named(control->engine_config_stack, build_rime_config(control), "rime");
    gtk_stack_add_named(control->engine_config_stack, build_mozc_config(control), "mozc");
    gtk_stack_set_visible_child_name(control->engine_config_stack, "empty");
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(control->engine_config_stack));

    return box;
}

static GtkWidget *build_voice_section(TypioControl *control) {
    GtkWidget *box = control_create_panel_box(14);
    GtkWidget *list = control_create_preferences_list();

    gtk_box_append(GTK_BOX(box),
                   control_create_section_header("Voice input",
                                         "Pick a backend, select an installed model, and manage local downloads."));

    control->voice_backend_model = gtk_string_list_new(nullptr);
    control->voice_backend_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->voice_backend_model), nullptr));
    g_signal_connect(control->voice_backend_dropdown, "notify::selected",
                     G_CALLBACK(on_voice_backend_changed), control);
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row("Voice backend",
                                              "Switch between Whisper and sherpa-onnx when both are available.",
                                              GTK_WIDGET(control->voice_backend_dropdown)));

    control->voice_model_list = gtk_string_list_new(nullptr);
    control->voice_model_dropdown = GTK_DROP_DOWN(
        gtk_drop_down_new(G_LIST_MODEL(control->voice_model_list), nullptr));
    g_signal_connect(control->voice_model_dropdown, "notify::selected",
                     G_CALLBACK(on_display_dropdown_changed), control);
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row("Installed model",
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
    GtkWidget *page = control_create_page_shell();
    GtkWidget *box = control_create_panel_box(14);
    GtkWidget *list = control_create_preferences_list();
    GtkEventController *key_ec;

    gtk_box_append(GTK_BOX(page),
                   control_create_section_header("Shortcuts",
                                         "Record combinations directly from the keyboard. Press Esc to cancel."));

    control->shortcut_switch_engine_btn = GTK_BUTTON(
        gtk_button_new_with_label("Ctrl+Shift"));
    g_signal_connect(control->shortcut_switch_engine_btn, "clicked",
                     G_CALLBACK(shortcut_btn_clicked), control);
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row("Switch engine",
                                              "Cycle the active engine without opening the panel.",
                                              GTK_WIDGET(control->shortcut_switch_engine_btn)));

    control->shortcut_voice_ptt_btn = GTK_BUTTON(
        gtk_button_new_with_label("Super+v"));
    g_signal_connect(control->shortcut_voice_ptt_btn, "clicked",
                     G_CALLBACK(shortcut_btn_clicked), control);
    gtk_list_box_append(GTK_LIST_BOX(list),
                        control_create_preference_row("Voice push-to-talk",
                                              "Hold the shortcut to activate the configured voice backend.",
                                              GTK_WIDGET(control->shortcut_voice_ptt_btn)));

    gtk_box_append(GTK_BOX(box), list);
    gtk_box_append(GTK_BOX(box),
                   control_create_empty_note("Click a shortcut button, then press the new key combination."));
    gtk_box_append(GTK_BOX(page), box);

    key_ec = gtk_event_controller_key_new();
    g_signal_connect(key_ec, "key-pressed", G_CALLBACK(shortcut_key_pressed), control);
    g_signal_connect(key_ec, "key-released", G_CALLBACK(shortcut_key_released), control);
    gtk_widget_add_controller(page, key_ec);
    return page;
}

GtkWidget *control_build_display_page(TypioControl *control) {
    GtkWidget *page = control_create_page_shell();
    GtkWidget *appearance_box = control_create_panel_box(14);
    GtkWidget *appearance_list = control_create_preferences_list();
    GtkWidget *notifications_box = control_create_panel_box(14);
    GtkWidget *notifications_list = control_create_preferences_list();

    gtk_box_append(GTK_BOX(page),
                   control_create_section_header("Appearance",
                                         "Tune the popup layout and panel notifications without leaving the session."));

    control->popup_theme_dropdown = GTK_DROP_DOWN(
        create_bound_widget(control, "engines.rime.popup_theme"));
    gtk_list_box_append(GTK_LIST_BOX(appearance_list),
                        control_create_preference_row("Popup theme",
                                              "Choose whether the candidate popup follows the desktop theme or a fixed appearance.",
                                              GTK_WIDGET(control->popup_theme_dropdown)));

    control->candidate_layout_dropdown = GTK_DROP_DOWN(
        create_bound_widget(control, "engines.rime.candidate_layout"));
    gtk_list_box_append(GTK_LIST_BOX(appearance_list),
                        control_create_preference_row("Candidate layout",
                                              "Select horizontal or vertical candidate arrangement for popup rendering.",
                                              GTK_WIDGET(control->candidate_layout_dropdown)));

    control->font_size_spin = GTK_SPIN_BUTTON(
        create_bound_widget(control, "engines.rime.font_size"));
    gtk_list_box_append(GTK_LIST_BOX(appearance_list),
                        control_create_preference_row("Font size",
                                              "Adjust the popup text size for candidate and preedit content.",
                                              GTK_WIDGET(control->font_size_spin)));

    gtk_box_append(GTK_BOX(appearance_box), appearance_list);
    gtk_box_append(GTK_BOX(page), appearance_box);

    gtk_box_append(GTK_BOX(page),
                   control_create_section_header("Notifications",
                                         "Keep runtime alerts useful without turning the panel into a dashboard."));

    control->notifications_enable_switch = GTK_SWITCH(
        create_bound_widget(control, "notifications.enable"));
    gtk_list_box_append(GTK_LIST_BOX(notifications_list),
                        control_create_preference_row("Enable notifications",
                                              "Master switch for panel-managed notification behavior.",
                                              GTK_WIDGET(control->notifications_enable_switch)));

    control->notifications_startup_switch = GTK_SWITCH(
        create_bound_widget(control, "notifications.startup_checks"));
    gtk_list_box_append(GTK_LIST_BOX(notifications_list),
                        control_create_preference_row("Startup checks",
                                              "Show startup warnings when Typio detects missing runtime prerequisites.",
                                              GTK_WIDGET(control->notifications_startup_switch)));

    control->notifications_runtime_switch = GTK_SWITCH(
        create_bound_widget(control, "notifications.runtime"));
    gtk_list_box_append(GTK_LIST_BOX(notifications_list),
                        control_create_preference_row("Runtime alerts",
                                              "Show alerts for service or backend issues during normal operation.",
                                              GTK_WIDGET(control->notifications_runtime_switch)));

    control->notifications_voice_switch = GTK_SWITCH(
        create_bound_widget(control, "notifications.voice"));
    gtk_list_box_append(GTK_LIST_BOX(notifications_list),
                        control_create_preference_row("Voice alerts",
                                              "Show voice-backend model and microphone related notifications.",
                                              GTK_WIDGET(control->notifications_voice_switch)));

    control->notifications_cooldown_spin = GTK_SPIN_BUTTON(
        create_bound_widget(control, "notifications.cooldown_ms"));
    gtk_list_box_append(GTK_LIST_BOX(notifications_list),
                        control_create_preference_row("Cooldown (ms)",
                                              "Debounce repeated notifications so transient issues do not spam the desktop.",
                                              GTK_WIDGET(control->notifications_cooldown_spin)));

    gtk_box_append(GTK_BOX(notifications_box), notifications_list);
    gtk_box_append(GTK_BOX(page), notifications_box);
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
    GtkWidget *page = control_create_page_shell();

    gtk_box_append(GTK_BOX(page),
                   control_create_section_header("Engines",
                                                 "Keep keyboard and voice backends in one place. Each section stays focused on a single job."));
    gtk_box_append(GTK_BOX(page), build_keyboard_section(control));
    gtk_box_append(GTK_BOX(page), build_voice_section(control));
    return page;
}
