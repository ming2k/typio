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
    return 0; /* fallback to first */
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
            gtk_drop_down_set_selected(GTK_DROP_DOWN(b->widget), idx);
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
            const char *val = (sel != GTK_INVALID_LIST_POSITION && f->ui_options[sel])
                ? f->ui_options[sel]
                : f->def.s;
            typio_config_set_string(config, f->key, val ? val : "");
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
