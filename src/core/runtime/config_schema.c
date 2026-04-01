/**
 * @file config_schema.c
 * @brief Static config schema registry — single source of truth for all
 *        configuration fields, their defaults, and UI metadata.
 */

#include "typio/config_schema.h"
#include "typio/config.h"
#include "typio/dbus_protocol.h"

#include <stdio.h>
#include <string.h>

/* ---------- dropdown option lists ---------- */

static const char *const popup_theme_options[] = {"auto", "light", "dark", NULL};
static const char *const candidate_layout_options[] = {"horizontal", "vertical", NULL};
static const char *const basic_printable_key_mode_options[] = {"forward", "commit", NULL};

/* ---------- The Schema Table ---------- */

static const TypioConfigField schema_fields[] = {
    /* --- Top-level --- */
    {
        .key = "default_engine",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
        .runtime_property = TYPIO_STATUS_PROP_ACTIVE_KEYBOARD_ENGINE,
    },
    {
        .key = "keyboard.per_app_preferences",
        .type = TYPIO_FIELD_BOOL,
        .def.b = true,
        .ui_label = "Per-app preferences",
        .ui_section = "keyboard",
    },

    /* --- Display (candidate popup) --- */
    {
        .key = "display.popup_theme",
        .type = TYPIO_FIELD_STRING,
        .def.s = "auto",
        .ui_label = "Theme",
        .ui_section = "display",
        .ui_options = popup_theme_options,
    },
    {
        .key = "display.candidate_layout",
        .type = TYPIO_FIELD_STRING,
        .def.s = "horizontal",
        .ui_label = "Layout",
        .ui_section = "display",
        .ui_options = candidate_layout_options,
    },
    {
        .key = "display.font_size",
        .type = TYPIO_FIELD_INT,
        .def.i = 11,
        .ui_label = "Font size",
        .ui_section = "display",
        .ui_min = 6, .ui_max = 72, .ui_step = 1,
    },
    {
        .key = "display.popup_mode_indicator",
        .type = TYPIO_FIELD_BOOL,
        .def.b = true,
        .ui_label = "Mode indicator",
        .ui_section = "display",
    },

    /* --- Notifications --- */
    {
        .key = "notifications.enable",
        .type = TYPIO_FIELD_BOOL,
        .def.b = true,
        .ui_label = "Enable",
        .ui_section = "notifications",
    },
    {
        .key = "notifications.startup_checks",
        .type = TYPIO_FIELD_BOOL,
        .def.b = true,
        .ui_label = "Startup checks",
        .ui_section = "notifications",
    },
    {
        .key = "notifications.runtime",
        .type = TYPIO_FIELD_BOOL,
        .def.b = true,
        .ui_label = "Runtime alerts",
        .ui_section = "notifications",
    },
    {
        .key = "notifications.voice",
        .type = TYPIO_FIELD_BOOL,
        .def.b = true,
        .ui_label = "Voice alerts",
        .ui_section = "notifications",
    },
    {
        .key = "notifications.cooldown_ms",
        .type = TYPIO_FIELD_INT,
        .def.i = 15000,
        .ui_label = "Cooldown (ms)",
        .ui_section = "notifications",
        .ui_min = 0, .ui_max = 300000, .ui_step = 1000,
    },

    /* --- Basic engine --- */
    {
        .key = "engines.basic.printable_key_mode",
        .type = TYPIO_FIELD_STRING,
        .def.s = "forward",
        .ui_label = "Printable keys",
        .ui_section = "basic",
        .ui_options = basic_printable_key_mode_options,
    },

    /* --- Rime engine --- */
    {
        .key = "engines.rime.shared_data_dir",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
    },
    {
        .key = "engines.rime.user_data_dir",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
    },
    {
        .key = "engines.rime.full_check",
        .type = TYPIO_FIELD_BOOL,
        .def.b = false,
    },

    /* --- Voice --- */
    {
        .key = "default_voice_engine",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
        .ui_label = "Voice Backend",
        .ui_section = "voice",
        .runtime_property = TYPIO_STATUS_PROP_ACTIVE_VOICE_ENGINE,
    },

    /* --- Shortcuts --- */
    {
        .key = "shortcuts.switch_engine",
        .type = TYPIO_FIELD_STRING,
        .def.s = "Ctrl+Shift",
        .ui_label = "Switch engine",
        .ui_section = "shortcuts",
    },
    {
        .key = "shortcuts.emergency_exit",
        .type = TYPIO_FIELD_STRING,
        .def.s = "Ctrl+Shift+Escape",
        .ui_label = "Emergency exit",
        .ui_section = "shortcuts",
    },
    {
        .key = "shortcuts.voice_ptt",
        .type = TYPIO_FIELD_STRING,
        .def.s = "Super+v",
        .ui_label = "Voice (PTT)",
        .ui_section = "shortcuts",
    },

    /* --- Voice engine configs --- */
    {
        .key = "engines.whisper.model",
        .type = TYPIO_FIELD_STRING,
        .def.s = "base",
    },
    {
        .key = "engines.whisper.language",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
    },
    {
        .key = "engines.sherpa-onnx.model",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
    },
    {
        .key = "engines.sherpa-onnx.language",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
    },
};

#define SCHEMA_FIELD_COUNT (sizeof(schema_fields) / sizeof(schema_fields[0]))

const TypioConfigField *typio_config_schema_fields(size_t *count) {
    if (count) {
        *count = SCHEMA_FIELD_COUNT;
    }
    return schema_fields;
}

const TypioConfigField *typio_config_schema_find(const char *key) {
    if (!key) {
        return NULL;
    }
    for (size_t i = 0; i < SCHEMA_FIELD_COUNT; i++) {
        if (strcmp(schema_fields[i].key, key) == 0) {
            return &schema_fields[i];
        }
    }
    return NULL;
}

const char *typio_config_schema_runtime_property(const char *key) {
    const TypioConfigField *field = typio_config_schema_find(key);

    if (!field || !field->runtime_property || !*field->runtime_property) {
        return NULL;
    }

    return field->runtime_property;
}

void typio_config_apply_defaults(TypioConfig *config) {
    if (!config) {
        return;
    }

    for (size_t i = 0; i < SCHEMA_FIELD_COUNT; i++) {
        const TypioConfigField *f = &schema_fields[i];
        if (typio_config_has_key(config, f->key)) {
            continue;
        }
        switch (f->type) {
        case TYPIO_FIELD_STRING:
            if (f->def.s && f->def.s[0]) {
                typio_config_set_string(config, f->key, f->def.s);
            }
            break;
        case TYPIO_FIELD_INT:
            typio_config_set_int(config, f->key, f->def.i);
            break;
        case TYPIO_FIELD_BOOL:
            typio_config_set_bool(config, f->key, f->def.b);
            break;
        case TYPIO_FIELD_FLOAT:
            typio_config_set_float(config, f->key, f->def.f);
            break;
        }
    }
}
