/**
 * @file main.c
 * @brief GTK4 control panel application logic for Typio
 */

#include "control_internal.h"
#include "control_bind.h"
#include "status/status.h"
#include "typio/rime_schema_list.h"
#include "typio/typio.h"

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

static char *variant_value_to_text(GVariant *value) {
    return g_variant_print(value, TRUE);
}

static char *control_dup_buffer_text(TypioControl *control) {
    GtkTextIter start;
    GtkTextIter end;

    if (!control || !control->config_buffer) {
        return nullptr;
    }

    gtk_text_buffer_get_bounds(control->config_buffer, &start, &end);
    return gtk_text_buffer_get_text(control->config_buffer, &start, &end, FALSE);
}

static char *control_dup_preferred_voice_backend(TypioControl *control,
                                                 const char *fallback_text) {
    char *content = NULL;
    TypioConfig *config = NULL;
    const char *backend = NULL;
    char *result = NULL;

    if (control && control->config_buffer) {
        content = control_dup_buffer_text(control);
    } else if (fallback_text) {
        content = g_strdup(fallback_text);
    }

    if (!content) {
        return NULL;
    }

    config = typio_config_load_string(content);
    g_free(content);
    if (!config) {
        return NULL;
    }

    backend = typio_config_get_string(config, "default_voice_engine", NULL);
    if (backend && *backend) {
        result = g_strdup(backend);
    }

    typio_config_free(config);
    return result;
}

static void control_update_window_title(TypioControl *control) {
    const char *title;

    if (!control || !control->window) {
        return;
    }

    title = control->config_dirty ? "Typio Control - Unsaved Changes"
                                  : "Typio Control";
    gtk_window_set_title(GTK_WINDOW(control->window), title);
}

static void control_update_config_actions(TypioControl *control) {
    gboolean service_available;
    gboolean can_apply;
    gboolean can_cancel;

    if (!control) {
        return;
    }

    service_available = control->proxy && g_dbus_proxy_get_name_owner(control->proxy);
    can_apply = service_available && control->config_dirty && !control->submitting_config;
    can_cancel = control->config_dirty && !control->submitting_config;

    if (control->apply_config_button) {
        gtk_widget_set_sensitive(GTK_WIDGET(control->apply_config_button), can_apply);
    }
    if (control->cancel_config_button) {
        gtk_widget_set_sensitive(GTK_WIDGET(control->cancel_config_button), can_cancel);
    }
}

static void control_set_dirty_state(TypioControl *control, gboolean dirty) {
    if (!control) {
        return;
    }

    control->config_dirty = dirty;
    if (control->config_status_label) {
        gtk_label_set_text(control->config_status_label,
                           dirty ? "Unsaved local changes. Apply to save or Cancel to discard."
                                 : "");
        gtk_widget_set_visible(GTK_WIDGET(control->config_status_label), dirty);
    }
    control_update_window_title(control);
    control_update_config_actions(control);
}

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

static void control_replace_staged_config_text(TypioControl *control,
                                               const char *text,
                                               gboolean mark_clean) {
    const char *resolved = control_resolve_config_text(text);

    if (!control || !control->config_buffer) {
        return;
    }

    control_begin_ui_sync(control);
    gtk_text_buffer_set_text(control->config_buffer, resolved, -1);
    control_sync_form_from_buffer(control);
    control_end_ui_sync(control);

    if (mark_clean) {
        control_set_dirty_state(control, FALSE);
    }
}

static void control_set_committed_config_text(TypioControl *control,
                                              const char *text) {
    if (!control) {
        return;
    }

    g_free(control->committed_config_text);
    control->committed_config_text = g_strdup(control_resolve_config_text(text));
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

static void control_update_engine_config_panel(TypioControl *control,
                                               const char *engine_name) {
    gboolean has_config = FALSE;

    if (!control || !control->engine_config_stack) {
        return;
    }

    if (engine_name && gtk_stack_get_child_by_name(control->engine_config_stack, engine_name)) {
        has_config = g_strcmp0(engine_name, "basic") != 0;
        gtk_stack_set_visible_child_name(control->engine_config_stack, engine_name);
    } else {
        gtk_stack_set_visible_child_name(control->engine_config_stack, "empty");
    }

    if (control->engine_config_title) {
        gtk_widget_set_visible(control->engine_config_title, has_config);
    }
}

static void control_set_config_text(TypioControl *control, GVariant *config_text) {
    const char *text = "";
    gboolean should_replace_stage;
    char *preferred_voice_backend;

    if (!control || !control->config_buffer) {
        return;
    }

    if (config_text && g_variant_is_of_type(config_text, G_VARIANT_TYPE_STRING)) {
        text = g_variant_get_string(config_text, nullptr);
    }

    should_replace_stage = !control->config_dirty ||
        (control->committed_config_text &&
         g_strcmp0(control->committed_config_text, text) != 0);

    g_debug("control_set_config_text: dirty=%d committed_matches=%d replace_stage=%d",
            control->config_dirty,
            control->committed_config_text &&
                g_strcmp0(control->committed_config_text, text) == 0,
            should_replace_stage);
    preferred_voice_backend = control_dup_preferred_voice_backend(control, text);
    g_debug("control_set_config_text: incoming default_voice_engine=%s",
            preferred_voice_backend ? preferred_voice_backend : "(unset)");
    g_free(preferred_voice_backend);

    control->config_seeded = TRUE;

    control_set_committed_config_text(control, text);
    if (should_replace_stage) {
        control_replace_staged_config_text(control, text, TRUE);
        return;
    }

    control_update_config_actions(control);
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

static void control_log_available_voice_backends(GVariant *engines) {
    GVariantIter iter;
    const char *name;
    guint count = 0;

    if (!engines || !g_variant_is_of_type(engines, G_VARIANT_TYPE("as"))) {
        g_debug("control_log_available_voice_backends: no AvailableEngines property");
        return;
    }

    g_variant_iter_init(&iter, engines);
    while (g_variant_iter_next(&iter, "&s", &name)) {
        if (!is_voice_backend_name(name)) {
            continue;
        }
        g_debug("control_log_available_voice_backends: backend[%u]=%s", count, name);
        count++;
    }

    g_debug("control_log_available_voice_backends: count=%u", count);
}

static void control_append_rime_schema(TypioControl *control, const char *schema_id) {
    guint count;

    if (!control || !control->rime_schema_model || !schema_id || !*schema_id) {
        return;
    }

    count = (guint)g_list_model_get_n_items(G_LIST_MODEL(control->rime_schema_model));
    for (guint i = 0; i < count; ++i) {
        const char *item = gtk_string_list_get_string(control->rime_schema_model, i);
        if (item && g_strcmp0(item, schema_id) == 0) {
            return;
        }
    }

    gtk_string_list_append(control->rime_schema_model, schema_id);
}

static void control_clear_rime_schema_model(TypioControl *control) {
    guint count;

    if (!control || !control->rime_schema_model) {
        return;
    }

    count = (guint)g_list_model_get_n_items(G_LIST_MODEL(control->rime_schema_model));
    gtk_string_list_splice(control->rime_schema_model, 0, count, NULL);
}

static void control_refresh_rime_schema_model(TypioControl *control,
                                              TypioConfig *parsed_config,
                                              const char *configured_schema) {
    gboolean was_updating_ui;
    TypioConfig *rime_config = NULL;
    TypioRimeSchemaList list;
    const char *default_data_dir = NULL;
    char *data_dir_buf = NULL;

    if (!control || !control->rime_schema_model) {
        return;
    }

    was_updating_ui = control_is_ui_syncing(control);
    control_begin_ui_sync(control);
    control_clear_rime_schema_model(control);

    /* Load schemas from the filesystem using the shared library */
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
    gtk_widget_set_sensitive(GTK_WIDGET(control->rime_schema_dropdown),
                             g_list_model_get_n_items(G_LIST_MODEL(control->rime_schema_model)) > 0);
    control->updating_ui = was_updating_ui;

    if (rime_config) {
        typio_config_free(rime_config);
    }
    g_free(data_dir_buf);
}

static void control_select_rime_schema_from_config(TypioControl *control, TypioConfig *config) {
    const char *schema;
    guint idx = GTK_INVALID_LIST_POSITION;

    if (!control || !control->rime_schema_dropdown) {
        return;
    }

    schema = config ? typio_config_get_string(config, "engines.rime.schema", NULL) : NULL;
    if (schema && *schema) {
        idx = control_find_model_index(control->rime_schema_model, schema);
    }
    if (idx == GTK_INVALID_LIST_POSITION &&
        g_list_model_get_n_items(G_LIST_MODEL(control->rime_schema_model)) > 0) {
        idx = 0;
    }
    gtk_drop_down_set_selected(control->rime_schema_dropdown, idx);
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

static void control_update_availability_label(TypioControl *control,
                                              const char *message,
                                              gboolean visible) {
    if (!control || !control->availability_label) {
        return;
    }

    gtk_label_set_text(control->availability_label, message ? message : "");
    gtk_widget_set_visible(GTK_WIDGET(control->availability_label), visible);
}

void control_sync_form_from_buffer(TypioControl *control) {
    GtkTextIter start;
    GtkTextIter end;
    char *content;
    TypioConfig *config;

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

    /* Bulk-load all schema-bound widgets */
    control_bindings_load_all(control->bindings, control->binding_count, config);

    /* Special cases that are not simple schema bindings */
    control_select_rime_schema_from_config(control, config);

    if (control->voice_backend_dropdown) {
        const char *voice_backend = typio_config_get_string(config,
                                                            "default_voice_engine",
                                                            nullptr);
        guint idx = control_voice_backend_index(control, voice_backend);
        if (idx == GTK_INVALID_LIST_POSITION &&
            g_list_model_get_n_items(G_LIST_MODEL(control->voice_backend_model)) > 0) {
            idx = 0;
        }
        g_debug("control_sync_form_from_buffer: default_voice_engine=%s voice_backend_index=%u",
                voice_backend ? voice_backend : "(unset)",
                idx);
        gtk_drop_down_set_selected(control->voice_backend_dropdown, idx);
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

    /* Bulk-save all schema-bound widgets */
    control_bindings_save_all(control->bindings, control->binding_count, config);

    if (control->rime_schema_dropdown && control->rime_schema_model) {
        selected = gtk_drop_down_get_selected(control->rime_schema_dropdown);
        if (selected != GTK_INVALID_LIST_POSITION) {
            const char *schema = gtk_string_list_get_string(control->rime_schema_model, selected);
            if (schema && *schema) {
                g_debug("control_sync_buffer_from_form: selected_rime_schema=%s", schema);
                typio_config_set_string(config, "engines.rime.schema", schema);
            }
        }
    }

    /* Special cases: voice backend dropdown with legacy removal */
    selected = gtk_drop_down_get_selected(control->voice_backend_dropdown);
    voice_backend = control_voice_backend_id(control, selected);
    if (!voice_backend) {
        voice_backend = "whisper";
    }
    g_debug("control_sync_buffer_from_form: voice_backend=%s (dropdown=%u)",
            voice_backend, selected);
    typio_config_remove(config, "voice.backend");
    typio_config_remove(config, "voice.model");
    typio_config_set_string(config, "default_voice_engine", voice_backend);

    /* Voice model selection */
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

    /* Shortcut recording buttons */
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
    control_set_dirty_state(control,
                            g_strcmp0(rendered, control->committed_config_text) != 0);
    free(rendered);
}

static void control_set_engine_model(TypioControl *control,
                                     GVariant *engines,
                                     const char *active_engine) {
    guint count = 0;

    if (!control || !control->engine_model) {
        return;
    }

    control_begin_ui_sync(control);
    count = (guint)g_list_model_get_n_items(G_LIST_MODEL(control->engine_model));
    gtk_string_list_splice(control->engine_model, 0, count, nullptr);

    if (engines && g_variant_is_of_type(engines, G_VARIANT_TYPE("as"))) {
        GVariantIter iter;
        const char *name;
        guint index = 0;
        guint selected = GTK_INVALID_LIST_POSITION;

        g_variant_iter_init(&iter, engines);
        while (g_variant_iter_next(&iter, "&s", &name)) {
            if (is_voice_backend_name(name)) {
                continue;
            }
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

    control_end_ui_sync(control);
}

static void control_set_voice_backend_model(TypioControl *control,
                                            GVariant *engines,
                                            const char *preferred_backend) {
    guint count;
    guint selected = GTK_INVALID_LIST_POSITION;

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

    if (preferred_backend) {
        selected = control_voice_backend_index(control, preferred_backend);
    }
    if (selected == GTK_INVALID_LIST_POSITION &&
        g_list_model_get_n_items(G_LIST_MODEL(control->voice_backend_model)) > 0) {
        selected = 0;
    }
    g_debug("control_set_voice_backend_model: preferred=%s selected=%u n_items=%u",
            preferred_backend ? preferred_backend : "(null)", selected,
            (guint)g_list_model_get_n_items(G_LIST_MODEL(control->voice_backend_model)));
    gtk_drop_down_set_selected(control->voice_backend_dropdown, selected);
    control_end_ui_sync(control);
}

void control_refresh_from_proxy(TypioControl *control) {
    GVariant *active_engine;
    GVariant *available_engines;
    GVariant *engine_state;
    GVariant *config_text;
    const char *active_name = "";
    const char *config_text_str = NULL;
    char *preferred_voice_backend = NULL;
    TypioConfig *parsed_config = NULL;
    const char *configured_schema = NULL;

    if (!control) {
        return;
    }

    if (!control->proxy || !g_dbus_proxy_get_name_owner(control->proxy)) {
        g_warning("control_refresh_from_proxy: Typio service unavailable");
        control_update_availability_label(control, "Typio service unavailable", TRUE);
        if (control->engine_label) {
            gtk_widget_set_visible(GTK_WIDGET(control->engine_label), FALSE);
        }
        control_clear_rime_schema_model(control);
        control_set_engine_model(control, nullptr, nullptr);
        preferred_voice_backend = control_dup_preferred_voice_backend(control, NULL);
        control_set_voice_backend_model(control, nullptr, preferred_voice_backend);
        g_free(preferred_voice_backend);
        control_set_state_text(control, nullptr);
        gtk_widget_set_sensitive(GTK_WIDGET(control->engine_dropdown), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(control->voice_backend_dropdown), FALSE);
        control_update_config_actions(control);
        return;
    }

    control_update_availability_label(control, "", FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(control->engine_dropdown), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(control->voice_backend_dropdown), TRUE);
    control_update_config_actions(control);

    active_engine = g_dbus_proxy_get_cached_property(control->proxy, "ActiveEngine");
    available_engines = g_dbus_proxy_get_cached_property(control->proxy, "AvailableEngines");
    engine_state = g_dbus_proxy_get_cached_property(control->proxy, "ActiveEngineState");
    config_text = g_dbus_proxy_get_cached_property(control->proxy, "ConfigText");

    if (active_engine) {
        active_name = g_variant_get_string(active_engine, nullptr);
    }
    if (control->engine_label) {
        gtk_widget_set_visible(GTK_WIDGET(control->engine_label), FALSE);
    }

    if (config_text && g_variant_is_of_type(config_text, G_VARIANT_TYPE_STRING)) {
        config_text_str = g_variant_get_string(config_text, NULL);
        parsed_config = typio_config_load_string(config_text_str);
        configured_schema = parsed_config
            ? typio_config_get_string(parsed_config, "engines.rime.schema", NULL)
            : NULL;
    }

    g_debug("control_refresh_from_proxy: entering");
    if (parsed_config) {
        const char *configured_voice =
            typio_config_get_string(parsed_config, "default_voice_engine", NULL);
        g_debug("control_refresh_from_proxy: config default_voice_engine=%s configured_schema=%s",
                configured_voice ? configured_voice : "(unset)",
                configured_schema ? configured_schema : "(unset)");
    }
    control_log_available_voice_backends(available_engines);
    control_set_engine_model(control, available_engines, active_name);
    control_refresh_rime_schema_model(control, parsed_config, configured_schema);
    preferred_voice_backend = control_dup_preferred_voice_backend(
        control,
        config_text && g_variant_is_of_type(config_text, G_VARIANT_TYPE_STRING)
            ? g_variant_get_string(config_text, NULL)
            : NULL);
    g_debug("control_refresh_from_proxy: preferred_voice_backend=%s",
            preferred_voice_backend ? preferred_voice_backend : "(null)");
    g_debug("control_refresh_from_proxy: active_engine=%s has_config=%d has_state=%d has_engines=%d",
            active_name && *active_name ? active_name : "(null)",
            config_text != NULL,
            engine_state != NULL,
            available_engines != NULL);
    control_set_voice_backend_model(control, available_engines, preferred_voice_backend);
    g_free(preferred_voice_backend);
    control_update_engine_config_panel(control, active_name);
    control_set_state_text(control, engine_state);
    control_set_config_text(control, config_text);

    if (engine_state) {
        g_variant_unref(engine_state);
    }
    if (config_text) {
        g_variant_unref(config_text);
    }
    if (available_engines) {
        g_variant_unref(available_engines);
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
                                   "ActivateEngine",
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
        control_update_config_actions(control);
        control_update_availability_label(control,
                                          error ? error->message
                                                : "Failed to apply configuration",
                                          TRUE);
        g_clear_error(&error);
        return;
    }

    g_variant_unref(reply);
    control->submitting_config = FALSE;
    g_debug("on_set_config_text_finished: success, refreshing from proxy");
    control_update_config_actions(control);
    control_refresh_from_proxy(control);
}

void control_stage_form_change(TypioControl *control) {
    if (!control || !control->config_seeded) {
        return;
    }

    control_sync_buffer_from_form(control);
}

void on_apply_config_clicked([[maybe_unused]] GtkButton *button, gpointer user_data) {
    TypioControl *control = user_data;
    char *content;
    TypioConfig *config;
    const char *voice_backend;

    if (!control || !control->proxy || !control->config_buffer ||
        control->submitting_config || !control->config_dirty) {
        return;
    }

    content = control_dup_buffer_text(control);
    if (!content) {
        return;
    }

    config = typio_config_load_string(content);
    voice_backend = config
        ? typio_config_get_string(config, "default_voice_engine", NULL)
        : NULL;
    g_message("Control apply: default_voice_engine=%s",
              voice_backend && *voice_backend ? voice_backend : "(unset)");
    if (config) {
        typio_config_free(config);
    }

    control->submitting_config = TRUE;
    control_update_config_actions(control);
    g_dbus_proxy_call(control->proxy,
                      "SetConfigText",
                      g_variant_new("(s)", content),
                      G_DBUS_CALL_FLAGS_NONE,
                      -1,
                      nullptr,
                      on_set_config_text_finished,
                      control);
    g_free(content);
}

void on_cancel_config_clicked([[maybe_unused]] GtkButton *button, gpointer user_data) {
    TypioControl *control = user_data;

    if (!control || control->submitting_config) {
        return;
    }

    control_replace_staged_config_text(control, control->committed_config_text, TRUE);
}

void on_form_spin_changed([[maybe_unused]] GtkSpinButton *spin, gpointer user_data) {
    control_stage_form_change((TypioControl *)user_data);
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
        control_stage_form_change(control);
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
            schema = gtk_string_list_get_string(control->rime_schema_model, selected);
            g_debug("on_display_dropdown_changed: rime_schema_selected=%s", schema ? schema : "(null)");
        }
        control_stage_form_change(control);
    }
}

void on_display_spin_changed([[maybe_unused]] GtkSpinButton *spin, gpointer user_data) {
    TypioControl *control = user_data;
    if (!control_is_ui_syncing(control)) {
        control_stage_form_change(control);
    }
}

void on_display_switch_changed(GObject *object,
                               [[maybe_unused]] GParamSpec *pspec,
                               gpointer user_data) {
    TypioControl *control = user_data;
    if (!control_is_ui_syncing(control) && GTK_IS_SWITCH(object)) {
        control_stage_form_change(control);
    }
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

    engine_name = gtk_string_list_get_string(control->engine_model, selected);
    control_activate_engine(control, engine_name);
    control_update_engine_config_panel(control, engine_name);
}

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
    control_clear_proxy(control);
    control_models_cleanup(control);
    g_free(control->committed_config_text);
    g_free(control);
}

static void activate(GtkApplication *app, gpointer user_data) {
    TypioControl *control = user_data;
    GtkWidget *window;
    GtkWidget *root;
    GtkWidget *nav_shell;
    GtkWidget *content_shell;
    GtkWidget *window_shell;
    GtkWidget *page_stack;
    GtkWidget *sidebar;
    GtkWidget *action_bar;
    GtkWidget *spacer;

    control->app = app;
    control->whisper_dir = g_build_filename(g_get_user_data_dir(), "typio", "whisper", nullptr);
    control->sherpa_dir = g_build_filename(g_get_user_data_dir(), "typio", "sherpa-onnx", nullptr);

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Typio Control");
    gtk_window_set_default_size(GTK_WINDOW(window), 760, 520);
    gtk_widget_set_size_request(window, 560, 400);
    gtk_widget_add_css_class(window, "control-root");
    control_apply_css();

    control->config_buffer = gtk_text_buffer_new(nullptr);

    window_shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    nav_shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    content_shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(window_shell, "window-shell");
    gtk_widget_add_css_class(window_shell, "background");
    gtk_widget_add_css_class(root, "control-root");
    gtk_widget_add_css_class(nav_shell, "nav-shell");
    gtk_widget_add_css_class(content_shell, "content-shell");
    gtk_widget_set_hexpand(window_shell, TRUE);
    gtk_widget_set_vexpand(window_shell, TRUE);
    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);
    gtk_widget_set_size_request(nav_shell, 168, -1);
    gtk_widget_set_vexpand(nav_shell, TRUE);
    gtk_widget_set_hexpand(content_shell, TRUE);
    gtk_widget_set_vexpand(content_shell, TRUE);
    gtk_box_append(GTK_BOX(root), nav_shell);
    gtk_box_append(GTK_BOX(root), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
    gtk_box_append(GTK_BOX(root), content_shell);

    page_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(page_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(page_stack, TRUE);
    gtk_widget_set_vexpand(page_stack, TRUE);

    sidebar = gtk_stack_sidebar_new();
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(page_stack));
    gtk_widget_add_css_class(sidebar, "nav-sidebar");
    gtk_widget_set_margin_top(sidebar, 16);
    gtk_widget_set_margin_bottom(sidebar, 16);
    gtk_widget_set_margin_start(sidebar, 12);
    gtk_widget_set_margin_end(sidebar, 12);
    gtk_widget_set_vexpand(sidebar, TRUE);
    gtk_widget_set_valign(sidebar, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(nav_shell), sidebar);

    control->availability_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(control->availability_label, 0.0f);
    gtk_widget_add_css_class(GTK_WIDGET(control->availability_label), "status-banner");
    gtk_widget_set_margin_top(GTK_WIDGET(control->availability_label), 16);
    gtk_widget_set_margin_start(GTK_WIDGET(control->availability_label), 16);
    gtk_widget_set_margin_end(GTK_WIDGET(control->availability_label), 16);
    gtk_widget_set_visible(GTK_WIDGET(control->availability_label), FALSE);
    gtk_box_append(GTK_BOX(content_shell), GTK_WIDGET(control->availability_label));

    control->engine_label = nullptr;

    gtk_stack_add_titled(GTK_STACK(page_stack),
                         control_wrap_page_scroller(control_build_display_page(control)),
                         "display", "Display");
    gtk_stack_add_titled(GTK_STACK(page_stack),
                         control_build_engines_page(control),
                         "engines", "Engines");
    gtk_stack_add_titled(GTK_STACK(page_stack),
                         control_wrap_page_scroller(control_build_shortcuts_page(control)),
                         "shortcuts", "Shortcuts");
    gtk_box_append(GTK_BOX(content_shell), page_stack);

    action_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_valign(action_bar, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(action_bar, 8);
    gtk_widget_set_margin_bottom(action_bar, 16);
    gtk_widget_set_margin_start(action_bar, 16);
    gtk_widget_set_margin_end(action_bar, 16);

    control->config_status_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(control->config_status_label, 0.0f);
    gtk_label_set_wrap(control->config_status_label, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(control->config_status_label), "inline-status");
    gtk_widget_set_halign(GTK_WIDGET(control->config_status_label), GTK_ALIGN_START);
    gtk_widget_set_valign(GTK_WIDGET(control->config_status_label), GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(GTK_WIDGET(control->config_status_label), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(control->config_status_label), FALSE);
    gtk_box_append(GTK_BOX(action_bar), GTK_WIDGET(control->config_status_label));

    spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(action_bar), spacer);

    control->cancel_config_button = GTK_BUTTON(gtk_button_new_with_label("Cancel"));
    g_signal_connect(control->cancel_config_button, "clicked",
                     G_CALLBACK(on_cancel_config_clicked), control);
    gtk_box_append(GTK_BOX(action_bar), GTK_WIDGET(control->cancel_config_button));

    control->apply_config_button = GTK_BUTTON(gtk_button_new_with_label("Apply"));
    g_signal_connect(control->apply_config_button, "clicked",
                     G_CALLBACK(on_apply_config_clicked), control);
    gtk_box_append(GTK_BOX(action_bar), GTK_WIDGET(control->apply_config_button));

    gtk_box_append(GTK_BOX(content_shell), action_bar);
    control_update_config_actions(control);

    gtk_box_append(GTK_BOX(window_shell), root);
    gtk_window_set_child(GTK_WINDOW(window), window_shell);
    control->window = window;
    control_update_window_title(control);

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
