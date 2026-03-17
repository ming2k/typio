/**
 * @file control_bind.c
 * @brief Schema-driven config ↔ GTK widget bindings.
 */

#include "control_bind.h"
#include "typio/config.h"

#include <string.h>

/* ---------- helpers ---------- */

static guint find_string_in_options(const char *const *options, const char *value) {
    if (!options || !value) {
        return GTK_INVALID_LIST_POSITION;
    }
    for (guint i = 0; options[i]; i++) {
        if (strcmp(options[i], value) == 0) {
            return i;
        }
    }
    return GTK_INVALID_LIST_POSITION;
}

static guint find_string_in_model(GListModel *model, const char *value) {
    guint count;

    if (!model || !value) {
        return GTK_INVALID_LIST_POSITION;
    }

    count = (guint)g_list_model_get_n_items(model);
    for (guint i = 0; i < count; ++i) {
        const char *item = gtk_string_list_get_string(GTK_STRING_LIST(model), i);
        if (item && strcmp(item, value) == 0) {
            return i;
        }
    }

    return GTK_INVALID_LIST_POSITION;
}

/* ---------- create ---------- */

GtkWidget *control_binding_create_widget(const TypioConfigField *field) {
    GtkWidget *widget = NULL;

    if (!field) {
        return NULL;
    }

    switch (field->type) {
    case TYPIO_FIELD_BOOL: {
        widget = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(widget), field->def.b);
        break;
    }
    case TYPIO_FIELD_INT: {
        double min = field->ui_min;
        double max = field->ui_max;
        double step = field->ui_step > 0 ? field->ui_step : 1;
        if (min == 0 && max == 0) {
            min = -999999;
            max = 999999;
        }
        GtkAdjustment *adj = gtk_adjustment_new(
            (double)field->def.i, min, max, step, step, 0.0);
        widget = gtk_spin_button_new(adj, step, 0);
        break;
    }
    case TYPIO_FIELD_STRING:
        if (field->ui_options) {
            GtkStringList *model = gtk_string_list_new(field->ui_options);
            widget = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
            guint idx = find_string_in_options(field->ui_options, field->def.s);
            if (idx != GTK_INVALID_LIST_POSITION) {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(widget), idx);
            }
        } else {
            widget = gtk_entry_new();
            if (field->def.s) {
                gtk_editable_set_text(GTK_EDITABLE(widget), field->def.s);
            }
        }
        break;
    case TYPIO_FIELD_FLOAT: {
        GtkAdjustment *adj = gtk_adjustment_new(
            field->def.f, -999999.0, 999999.0, 0.1, 1.0, 0.0);
        widget = gtk_spin_button_new(adj, 0.1, 2);
        break;
    }
    }

    if (!widget) {
        return NULL;
    }

    gtk_widget_add_css_class(widget, "control-field");
    if (GTK_IS_DROP_DOWN(widget)) {
        gtk_widget_add_css_class(widget, "control-dropdown");
    } else if (GTK_IS_SPIN_BUTTON(widget)) {
        gtk_widget_add_css_class(widget, "control-spin");
    } else if (GTK_IS_ENTRY(widget)) {
        gtk_widget_add_css_class(widget, "control-entry");
    } else if (GTK_IS_SWITCH(widget)) {
        gtk_widget_add_css_class(widget, "control-switch");
    }

    return widget;
}

/* ---------- load ---------- */

void control_binding_load(const ControlBinding *b, const TypioConfig *config) {
    if (!b || !b->field || !b->widget || !config) {
        return;
    }

    const TypioConfigField *f = b->field;

    switch (f->type) {
    case TYPIO_FIELD_BOOL:
        gtk_switch_set_active(GTK_SWITCH(b->widget),
                              typio_config_get_bool(config, f->key, f->def.b));
        break;

    case TYPIO_FIELD_INT:
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(b->widget),
                                  typio_config_get_int(config, f->key, f->def.i));
        break;

    case TYPIO_FIELD_STRING:
        if (f->ui_options) {
            const char *val = typio_config_get_string(config, f->key, f->def.s);
            guint idx = find_string_in_options(f->ui_options, val);
            GtkStringList *model = GTK_STRING_LIST(gtk_drop_down_get_model(GTK_DROP_DOWN(b->widget)));
            if (idx == GTK_INVALID_LIST_POSITION && val && *val && model) {
                guint existing = find_string_in_model(G_LIST_MODEL(model), val);
                if (existing == GTK_INVALID_LIST_POSITION) {
                    gtk_string_list_append(model, val);
                    existing = (guint)g_list_model_get_n_items(G_LIST_MODEL(model)) - 1;
                }
                idx = existing;
            }
            if (idx != GTK_INVALID_LIST_POSITION) {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(b->widget), idx);
            } else {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(b->widget),
                                           GTK_INVALID_LIST_POSITION);
            }
        } else {
            const char *val = typio_config_get_string(config, f->key,
                                                       f->def.s ? f->def.s : "");
            gtk_editable_set_text(GTK_EDITABLE(b->widget), val);
        }
        break;

    case TYPIO_FIELD_FLOAT:
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(b->widget),
                                  typio_config_get_float(config, f->key, f->def.f));
        break;
    }
}

/* ---------- save ---------- */

void control_binding_save(const ControlBinding *b, TypioConfig *config) {
    if (!b || !b->field || !b->widget || !config) {
        return;
    }

    const TypioConfigField *f = b->field;

    switch (f->type) {
    case TYPIO_FIELD_BOOL:
        typio_config_set_bool(config, f->key,
                              gtk_switch_get_active(GTK_SWITCH(b->widget)));
        break;

    case TYPIO_FIELD_INT:
        typio_config_set_int(config, f->key,
                             gtk_spin_button_get_value_as_int(
                                 GTK_SPIN_BUTTON(b->widget)));
        break;

    case TYPIO_FIELD_STRING:
        if (f->ui_options) {
            guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(b->widget));
            GtkStringList *model = GTK_STRING_LIST(gtk_drop_down_get_model(GTK_DROP_DOWN(b->widget)));
            if (sel != GTK_INVALID_LIST_POSITION && model) {
                const char *val = gtk_string_list_get_string(model, sel);
                if (val) {
                    typio_config_set_string(config, f->key, val);
                }
            }
        } else {
            const char *text = gtk_editable_get_text(GTK_EDITABLE(b->widget));
            typio_config_set_string(config, f->key, text ? text : "");
        }
        break;

    case TYPIO_FIELD_FLOAT:
        typio_config_set_float(config, f->key,
                               gtk_spin_button_get_value(
                                   GTK_SPIN_BUTTON(b->widget)));
        break;
    }
}

/* ---------- batch ---------- */

void control_bindings_load_all(const ControlBinding *bindings, size_t count,
                               const TypioConfig *config) {
    for (size_t i = 0; i < count; i++) {
        control_binding_load(&bindings[i], config);
    }
}

void control_bindings_save_all(const ControlBinding *bindings, size_t count,
                               TypioConfig *config) {
    for (size_t i = 0; i < count; i++) {
        control_binding_save(&bindings[i], config);
    }
}

void control_state_binding_select_value(const ControlStateBinding *binding,
                                        const char *value) {
    guint idx = GTK_INVALID_LIST_POSITION;

    if (!binding || !binding->dropdown || !binding->find_index) {
        return;
    }

    if (value && *value) {
        idx = binding->find_index(binding->user_data, value);
    }

    gtk_drop_down_set_selected(binding->dropdown, idx);
}

void control_state_binding_load_from_config(const ControlStateBinding *binding,
                                            const TypioConfig *config) {
    const char *value;

    if (!binding || !binding->config_key || !config) {
        return;
    }

    value = typio_config_get_string(config, binding->config_key, NULL);
    control_state_binding_select_value(binding, value);
}

const char *control_state_binding_get_selected_value(const ControlStateBinding *binding) {
    guint selected;

    if (!binding || !binding->dropdown || !binding->get_value) {
        return NULL;
    }

    selected = gtk_drop_down_get_selected(binding->dropdown);
    if (selected == GTK_INVALID_LIST_POSITION) {
        return NULL;
    }

    return binding->get_value(binding->user_data, selected);
}

void control_state_binding_save_to_config(const ControlStateBinding *binding,
                                          TypioConfig *config) {
    const char *value;

    if (!binding || !binding->config_key || !config) {
        return;
    }

    value = control_state_binding_get_selected_value(binding);
    if (value && *value) {
        typio_config_set_string(config, binding->config_key, value);
    }
}

void control_state_binding_refresh_options(const ControlStateBinding *binding,
                                           const TypioConfig *config,
                                           const char *configured_value) {
    if (!binding || !binding->refresh_options) {
        return;
    }

    binding->refresh_options(binding->options_user_data, config, configured_value);
}
