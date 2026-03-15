/**
 * @file shortcut_config.h
 * @brief Configurable keyboard shortcut bindings
 *
 * Parses shortcut strings (e.g. "Ctrl+Shift", "Super+v") into
 * modifier bitmasks and optional keysyms for runtime matching.
 */

#ifndef TYPIO_WL_SHORTCUT_CONFIG_H
#define TYPIO_WL_SHORTCUT_CONFIG_H

#include "typio/types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A single shortcut binding: modifier mask + optional keysym.
 * If keysym == 0, this is a modifier-only chord (e.g. Ctrl+Shift).
 */
typedef struct {
    uint32_t modifiers;     /* TYPIO_MOD_* bitmask */
    uint32_t keysym;        /* XKB keysym, or 0 for modifier-only */
} TypioShortcutBinding;

/**
 * All configurable shortcut actions.
 */
typedef struct {
    TypioShortcutBinding switch_engine;     /* default: Ctrl+Shift */
    TypioShortcutBinding voice_ptt;         /* default: Super+v */
} TypioShortcutConfig;

/**
 * Load shortcut config from a TypioConfig.
 * Reads from [shortcuts] section; missing keys get defaults.
 */
void typio_shortcut_config_load(TypioShortcutConfig *sc,
                                const TypioConfig *config);

/**
 * Fill with built-in defaults (Ctrl+Shift, Super+v).
 */
void typio_shortcut_config_defaults(TypioShortcutConfig *sc);

/**
 * Parse a shortcut string like "Ctrl+Shift" or "Super+v"
 * into a TypioShortcutBinding.  Returns true on success.
 */
bool typio_shortcut_parse(const char *str, TypioShortcutBinding *out);

/**
 * Format a binding back to a human-readable string.
 * Caller must free() the result.
 */
char *typio_shortcut_format(const TypioShortcutBinding *binding);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_SHORTCUT_CONFIG_H */
