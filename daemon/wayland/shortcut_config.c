/**
 * @file shortcut_config.c
 * @brief Configurable keyboard shortcut bindings — parsing and defaults
 */

#include "shortcut_config.h"

#include "typio/config.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <xkbcommon/xkbcommon-keysyms.h>

/* ── Modifier name table ─────────────────────────────────────────── */

typedef struct {
    const char *name;
    uint32_t modifier;
} ModifierName;

static const ModifierName modifier_names[] = {
    { "ctrl",    TYPIO_MOD_CTRL },
    { "control", TYPIO_MOD_CTRL },
    { "shift",   TYPIO_MOD_SHIFT },
    { "alt",     TYPIO_MOD_ALT },
    { "super",   TYPIO_MOD_SUPER },
};

#define MODIFIER_NAME_COUNT \
    (sizeof(modifier_names) / sizeof(modifier_names[0]))

/* ── Keysym lookup for common names ──────────────────────────────── */

typedef struct {
    const char *name;
    uint32_t keysym;
} KeysymName;

static const KeysymName keysym_names[] = {
    { "space",     XKB_KEY_space },
    { "return",    XKB_KEY_Return },
    { "enter",     XKB_KEY_Return },
    { "tab",       XKB_KEY_Tab },
    { "escape",    XKB_KEY_Escape },
    { "esc",       XKB_KEY_Escape },
    { "backspace", XKB_KEY_BackSpace },
    { "delete",    XKB_KEY_Delete },
    { "up",        XKB_KEY_Up },
    { "down",      XKB_KEY_Down },
    { "left",      XKB_KEY_Left },
    { "right",     XKB_KEY_Right },
    { "f1",        XKB_KEY_F1 },
    { "f2",        XKB_KEY_F2 },
    { "f3",        XKB_KEY_F3 },
    { "f4",        XKB_KEY_F4 },
    { "f5",        XKB_KEY_F5 },
    { "f6",        XKB_KEY_F6 },
    { "f7",        XKB_KEY_F7 },
    { "f8",        XKB_KEY_F8 },
    { "f9",        XKB_KEY_F9 },
    { "f10",       XKB_KEY_F10 },
    { "f11",       XKB_KEY_F11 },
    { "f12",       XKB_KEY_F12 },
};

#define KEYSYM_NAME_COUNT \
    (sizeof(keysym_names) / sizeof(keysym_names[0]))

/* ── Case-insensitive comparison ─────────────────────────────────── */

static bool str_eq_nocase(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* ── Parsing ─────────────────────────────────────────────────────── */

static uint32_t parse_modifier(const char *token) {
    for (size_t i = 0; i < MODIFIER_NAME_COUNT; i++) {
        if (str_eq_nocase(token, modifier_names[i].name))
            return modifier_names[i].modifier;
    }
    return 0;
}

static uint32_t parse_keysym(const char *token) {
    /* Named keys */
    for (size_t i = 0; i < KEYSYM_NAME_COUNT; i++) {
        if (str_eq_nocase(token, keysym_names[i].name))
            return keysym_names[i].keysym;
    }

    /* Single character → XKB latin keysym (lowercase) */
    if (token[0] != '\0' && token[1] == '\0') {
        char ch = (char)tolower((unsigned char)token[0]);
        if (ch >= 'a' && ch <= 'z')
            return (uint32_t)ch;  /* XKB_KEY_a .. XKB_KEY_z */
        if (ch >= '0' && ch <= '9')
            return (uint32_t)ch;  /* XKB_KEY_0 .. XKB_KEY_9 */
    }

    return 0;
}

bool typio_shortcut_parse(const char *str, TypioShortcutBinding *out) {
    if (!str || !out)
        return false;

    out->modifiers = 0;
    out->keysym = 0;

    /* Work on a mutable copy */
    size_t len = strlen(str);
    if (len == 0 || len > 128)
        return false;

    char buf[129];
    memcpy(buf, str, len + 1);

    /* Split on '+' */
    char *saveptr = NULL;
    char *token = strtok_r(buf, "+", &saveptr);
    uint32_t last_keysym = 0;
    uint32_t last_modifier = 0;
    int token_count = 0;

    while (token) {
        /* Trim whitespace */
        while (*token == ' ') token++;
        char *end = token + strlen(token);
        while (end > token && *(end - 1) == ' ') end--;
        *end = '\0';

        if (*token == '\0') {
            token = strtok_r(NULL, "+", &saveptr);
            continue;
        }

        token_count++;

        /* Try as modifier first */
        uint32_t mod = parse_modifier(token);
        if (mod != 0) {
            out->modifiers |= mod;
            last_modifier = mod;
        } else {
            /* Must be a keysym — only one allowed */
            uint32_t ks = parse_keysym(token);
            if (ks == 0)
                return false;  /* unknown token */
            if (last_keysym != 0)
                return false;  /* multiple non-modifier keys */
            last_keysym = ks;
        }

        token = strtok_r(NULL, "+", &saveptr);
    }

    if (token_count == 0)
        return false;

    out->keysym = last_keysym;

    /* Must have at least one modifier */
    if (out->modifiers == 0 && last_modifier == 0)
        return false;

    return true;
}

/* ── Formatting ──────────────────────────────────────────────────── */

char *typio_shortcut_format(const TypioShortcutBinding *binding) {
    if (!binding)
        return NULL;

    char buf[128] = "";
    size_t pos = 0;

    /* Modifiers in canonical order */
    static const struct { uint32_t mod; const char *name; } order[] = {
        { TYPIO_MOD_CTRL,  "Ctrl" },
        { TYPIO_MOD_ALT,   "Alt" },
        { TYPIO_MOD_SUPER, "Super" },
        { TYPIO_MOD_SHIFT, "Shift" },
    };

    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (binding->modifiers & order[i].mod) {
            if (pos > 0)
                buf[pos++] = '+';
            size_t n = strlen(order[i].name);
            memcpy(buf + pos, order[i].name, n);
            pos += n;
        }
    }

    /* Keysym */
    if (binding->keysym != 0) {
        if (pos > 0)
            buf[pos++] = '+';

        /* Check named keys */
        bool found = false;
        for (size_t i = 0; i < KEYSYM_NAME_COUNT; i++) {
            if (keysym_names[i].keysym == binding->keysym) {
                /* Capitalize first letter */
                size_t n = strlen(keysym_names[i].name);
                memcpy(buf + pos, keysym_names[i].name, n);
                buf[pos] = (char)toupper((unsigned char)buf[pos]);
                pos += n;
                found = true;
                break;
            }
        }

        /* Single character */
        if (!found && binding->keysym >= 'a' && binding->keysym <= 'z') {
            buf[pos++] = (char)binding->keysym;
            found = true;
        }
        if (!found && binding->keysym >= '0' && binding->keysym <= '9') {
            buf[pos++] = (char)binding->keysym;
            found = true;
        }

        if (!found) {
            /* Fallback: hex */
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos,
                                    "0x%x", binding->keysym);
        }
    }

    buf[pos] = '\0';
    return strdup(buf);
}

/* ── Config loading ──────────────────────────────────────────────── */

void typio_shortcut_config_defaults(TypioShortcutConfig *sc) {
    if (!sc) return;

    /* Ctrl+Shift chord for engine switching */
    sc->switch_engine.modifiers = TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT;
    sc->switch_engine.keysym = 0;

    /* Ctrl+Shift+Escape for emergency shutdown */
    sc->emergency_exit.modifiers = TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT;
    sc->emergency_exit.keysym = XKB_KEY_Escape;

    /* Super+v for voice push-to-talk */
    sc->voice_ptt.modifiers = TYPIO_MOD_SUPER;
    sc->voice_ptt.keysym = XKB_KEY_v;
}

void typio_shortcut_config_load(TypioShortcutConfig *sc,
                                const TypioConfig *config) {
    if (!sc) return;

    typio_shortcut_config_defaults(sc);

    if (!config) return;

    const char *val;
    TypioShortcutBinding parsed;

    val = typio_config_get_string(config, "shortcuts.switch_engine", NULL);
    if (val && typio_shortcut_parse(val, &parsed))
        sc->switch_engine = parsed;

    val = typio_config_get_string(config, "shortcuts.emergency_exit", NULL);
    if (val && typio_shortcut_parse(val, &parsed))
        sc->emergency_exit = parsed;

    val = typio_config_get_string(config, "shortcuts.voice_ptt", NULL);
    if (val && typio_shortcut_parse(val, &parsed))
        sc->voice_ptt = parsed;
}
