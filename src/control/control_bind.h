/**
 * @file control_bind.h
 * @brief Schema-driven config ↔ GTK widget bindings for the control panel.
 */

#ifndef TYPIO_CONTROL_BIND_H
#define TYPIO_CONTROL_BIND_H

#include "typio/config_schema.h"
#include <gtk/gtk.h>

typedef struct ControlBinding {
    const TypioConfigField *field;
    GtkWidget *widget;
} ControlBinding;

/**
 * Create a GTK widget appropriate for the field type:
 *  BOOL → GtkSwitch, INT (with bounds) → GtkSpinButton,
 *  STRING+options → GtkDropDown, STRING → GtkEntry.
 */
GtkWidget *control_binding_create_widget(const TypioConfigField *field);

/**
 * Load a single binding's value from config into its widget.
 */
void control_binding_load(const ControlBinding *b, const TypioConfig *config);

/**
 * Save a single binding's widget value back into config.
 */
void control_binding_save(const ControlBinding *b, TypioConfig *config);

/**
 * Load all bindings in the array from config.
 */
void control_bindings_load_all(const ControlBinding *bindings, size_t count,
                               const TypioConfig *config);

/**
 * Save all bindings in the array to config.
 */
void control_bindings_save_all(const ControlBinding *bindings, size_t count,
                               TypioConfig *config);

#endif /* TYPIO_CONTROL_BIND_H */
