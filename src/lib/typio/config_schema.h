/**
 * @file config_schema.h
 * @brief Static config schema registry — single source of truth for all
 *        configuration fields, their defaults, legacy aliases, and UI metadata.
 */

#ifndef TYPIO_CONFIG_SCHEMA_H
#define TYPIO_CONFIG_SCHEMA_H

#include "config.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TYPIO_FIELD_STRING = 0,
    TYPIO_FIELD_INT = 1,
    TYPIO_FIELD_BOOL = 2,
    TYPIO_FIELD_FLOAT = 3,
} TypioFieldType;

typedef struct TypioConfigField {
    const char *key;            /* canonical dotted key, e.g. "engines.rime.font_size" */
    TypioFieldType type;
    union {
        const char *s;
        int i;
        bool b;
        double f;
    } def;                      /* default value */
    const char *legacy_key;     /* first legacy alias, or NULL */
    const char *legacy_key2;    /* second legacy alias, or NULL */

    /* UI metadata (ignored by server-side code) */
    const char *ui_label;
    const char *ui_section;     /* "display"|"notifications"|"rime"|"mozc"|"shortcuts"|"voice" */
    int ui_min, ui_max, ui_step;
    const char *const *ui_options; /* NULL-terminated string array for dropdowns, or NULL */
    const char *runtime_property;  /* matching D-Bus runtime property, or NULL */
} TypioConfigField;

/**
 * @brief Look up a schema field by canonical key.
 * @return Pointer into static table, or NULL if not found.
 */
const TypioConfigField *typio_config_schema_find(const char *key);

/**
 * @brief Get the runtime D-Bus property mirrored by a persisted config key.
 * @return Property name, or NULL if the key has no direct runtime mirror.
 */
const char *typio_config_schema_runtime_property(const char *key);

/**
 * @brief Apply default values from the schema for any key not already present.
 */
void typio_config_apply_defaults(TypioConfig *config);

/**
 * @brief Migrate legacy keys to their canonical equivalents.
 *
 * For each field that has a legacy_key (or legacy_key2), if the legacy key
 * exists in @p config but the canonical key does not, copy the value and
 * remove the legacy entry.
 */
void typio_config_migrate_legacy(TypioConfig *config);

/**
 * @brief Get the full schema table.
 * @param[out] count Number of entries.
 * @return Pointer to static array.
 */
const TypioConfigField *typio_config_schema_fields(size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_CONFIG_SCHEMA_H */
