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

typedef guint (*ControlStateIndexFunc)(gpointer user_data, const char *value);
typedef const char *(*ControlStateValueFunc)(gpointer user_data, guint index);
typedef void (*ControlStateOptionsRefreshFunc)(gpointer user_data,
                                               const TypioConfig *config,
                                               const char *configured_value);

typedef enum ControlStateValueSource {
    CONTROL_STATE_VALUE_FROM_CONFIG = 0,
    CONTROL_STATE_VALUE_FROM_RUNTIME = 1,
    CONTROL_STATE_VALUE_RUNTIME_THEN_CONFIG = 2,
} ControlStateValueSource;

typedef struct ControlStateBinding {
    const char *config_key;
    GtkDropDown *dropdown;
    gpointer user_data;
    ControlStateIndexFunc find_index;
    ControlStateValueFunc get_value;
    ControlStateValueSource source;
    gpointer options_user_data;
    ControlStateOptionsRefreshFunc refresh_options;
} ControlStateBinding;

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

void control_state_binding_select_value(const ControlStateBinding *binding,
                                        const char *value);
void control_state_binding_load_from_config(const ControlStateBinding *binding,
                                            const TypioConfig *config);
const char *control_state_binding_get_selected_value(const ControlStateBinding *binding);
void control_state_binding_save_to_config(const ControlStateBinding *binding,
                                          TypioConfig *config);
void control_state_binding_refresh_options(const ControlStateBinding *binding,
                                           const TypioConfig *config,
                                           const char *configured_value);

#endif /* TYPIO_CONTROL_BIND_H */
