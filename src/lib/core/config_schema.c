/**
 * @file config_schema.c
 * @brief Static config schema registry — single source of truth for all
 *        configuration fields, their defaults, legacy aliases, and UI metadata.
 */

#include "typio/config_schema.h"
#include "typio/config.h"

#include <stdio.h>
#include <string.h>

/* ---------- dropdown option lists ---------- */

static const char *const popup_theme_options[] = {"auto", "light", "dark", NULL};
static const char *const candidate_layout_options[] = {"horizontal", "vertical", NULL};

/* ---------- The Schema Table ---------- */

static const TypioConfigField schema_fields[] = {
    /* --- Top-level --- */
    {
        .key = "default_engine",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
    },

    /* --- Display / Rime popup --- */
    {
        .key = "engines.rime.popup_theme",
        .type = TYPIO_FIELD_STRING,
        .def.s = "auto",
        .ui_label = "Theme",
        .ui_section = "display",
        .ui_options = popup_theme_options,
    },
    {
        .key = "engines.rime.candidate_layout",
        .type = TYPIO_FIELD_STRING,
        .def.s = "horizontal",
        .ui_label = "Layout",
        .ui_section = "display",
        .ui_options = candidate_layout_options,
    },
    {
        .key = "engines.rime.font_size",
        .type = TYPIO_FIELD_INT,
        .def.i = 11,
        .ui_label = "Font size",
        .ui_section = "display",
        .ui_min = 6, .ui_max = 72, .ui_step = 1,
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

    /* --- Rime engine --- */
    {
        .key = "engines.rime.schema",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
        .ui_label = "Schema",
        .ui_section = "rime",
    },
    {
        .key = "engines.rime.page_size",
        .type = TYPIO_FIELD_INT,
        .def.i = 9,
        .ui_label = "Page size",
        .ui_section = "rime",
        .ui_min = 1, .ui_max = 20, .ui_step = 1,
    },
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

    /* --- Mozc engine --- */
    {
        .key = "engines.mozc.page_size",
        .type = TYPIO_FIELD_INT,
        .def.i = 9,
        .ui_label = "Page size",
        .ui_section = "mozc",
        .ui_min = 1, .ui_max = 20, .ui_step = 1,
    },

    /* --- Voice --- */
    {
        .key = "default_voice_engine",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
        .legacy_key = "voice.backend",
        .ui_label = "Voice Backend",
        .ui_section = "voice",
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
        .key = "shortcuts.voice_ptt",
        .type = TYPIO_FIELD_STRING,
        .def.s = "Super+v",
        .ui_label = "Voice (PTT)",
        .ui_section = "shortcuts",
    },

    /* --- Voice engine configs (no UI, but need defaults/legacy migration) ---
     *
     * NOTE: voice.model and voice.language are shared legacy keys that must be
     * routed to the correct engine based on default_voice_engine.  They are NOT
     * listed as legacy_key here; instead they are handled by the dedicated
     * migrate_voice_shared_keys() in typio_config_migrate_legacy().
     *
     * Only whisper.model / whisper.language (unambiguous) use legacy_key2.
     */
    {
        .key = "engines.whisper.model",
        .type = TYPIO_FIELD_STRING,
        .def.s = "base",
        .legacy_key2 = "whisper.model",
    },
    {
        .key = "engines.whisper.language",
        .type = TYPIO_FIELD_STRING,
        .def.s = "",
        .legacy_key2 = "whisper.language",
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

/**
 * Copy a config value from @p src_key to @p dst_key if dst doesn't exist yet.
 * Removes the source key after copying.
 */
static void migrate_one(TypioConfig *config,
                         const char *src_key,
                         const char *dst_key) {
    if (!typio_config_has_key(config, src_key)) {
        return;
    }
    if (typio_config_has_key(config, dst_key)) {
        /* Canonical key already set — just remove legacy */
        typio_config_remove(config, src_key);
        return;
    }

    const TypioConfigValue *val = typio_config_get(config, src_key);
    if (!val) {
        return;
    }

    switch (val->type) {
    case TYPIO_CONFIG_STRING:
        typio_config_set_string(config, dst_key, val->data.string_val);
        break;
    case TYPIO_CONFIG_INT:
        typio_config_set_int(config, dst_key, val->data.int_val);
        break;
    case TYPIO_CONFIG_BOOL:
        typio_config_set_bool(config, dst_key, val->data.bool_val);
        break;
    case TYPIO_CONFIG_FLOAT:
        typio_config_set_float(config, dst_key, val->data.float_val);
        break;
    default:
        break;
    }

    typio_config_remove(config, src_key);
}

/**
 * Migrate the shared voice.model / voice.language legacy keys.
 *
 * These keys are ambiguous: the original code routed them to the engine
 * indicated by voice.backend.  We replicate that logic here by checking
 * the already-resolved default_voice_engine (which was migrated from
 * voice.backend in the generic pass above).
 */
static void migrate_voice_shared_keys(TypioConfig *config) {
    const char *engine;
    char dst_model[128];
    char dst_language[128];

    if (!typio_config_has_key(config, "voice.model") &&
        !typio_config_has_key(config, "voice.language")) {
        return;
    }

    /* Determine which engine the old [voice] section targeted. */
    engine = typio_config_get_string(config, "default_voice_engine", NULL);
    if (!engine || !*engine) {
        /* No backend configured — default to whisper (matches old behaviour). */
        engine = "whisper";
    }

    snprintf(dst_model, sizeof(dst_model), "engines.%s.model", engine);
    snprintf(dst_language, sizeof(dst_language), "engines.%s.language", engine);

    migrate_one(config, "voice.model", dst_model);
    migrate_one(config, "voice.language", dst_language);
}

void typio_config_migrate_legacy(TypioConfig *config) {
    if (!config) {
        return;
    }

    /* Pass 1: generic per-field migration (handles voice.backend →
     * default_voice_engine and whisper.* → engines.whisper.*). */
    for (size_t i = 0; i < SCHEMA_FIELD_COUNT; i++) {
        const TypioConfigField *f = &schema_fields[i];
        if (f->legacy_key) {
            migrate_one(config, f->legacy_key, f->key);
        }
        if (f->legacy_key2) {
            migrate_one(config, f->legacy_key2, f->key);
        }
    }

    /* Pass 2: shared voice keys that depend on which backend was selected. */
    migrate_voice_shared_keys(config);
}
