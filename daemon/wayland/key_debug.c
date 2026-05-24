/**
 * @file key_debug.c
 * @brief Helpers for readable key debug output
 */

#include "key_debug.h"

#include <stdio.h>
#include <xkbcommon/xkbcommon.h>

static int key_debug_is_valid_unicode(uint32_t unicode) {
    return unicode <= 0x10ffff &&
           !(unicode >= 0xd800 && unicode <= 0xdfff);
}

static size_t key_debug_encode_utf8(uint32_t unicode, char out[5]) {
    if (!key_debug_is_valid_unicode(unicode))
        return 0;

    if (unicode <= 0x7f) {
        out[0] = (char)unicode;
        out[1] = '\0';
        return 1;
    }

    if (unicode <= 0x7ff) {
        out[0] = (char)(0xc0 | (unicode >> 6));
        out[1] = (char)(0x80 | (unicode & 0x3f));
        out[2] = '\0';
        return 2;
    }

    if (unicode <= 0xffff) {
        out[0] = (char)(0xe0 | (unicode >> 12));
        out[1] = (char)(0x80 | ((unicode >> 6) & 0x3f));
        out[2] = (char)(0x80 | (unicode & 0x3f));
        out[3] = '\0';
        return 3;
    }

    out[0] = (char)(0xf0 | (unicode >> 18));
    out[1] = (char)(0x80 | ((unicode >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((unicode >> 6) & 0x3f));
    out[3] = (char)(0x80 | (unicode & 0x3f));
    out[4] = '\0';
    return 4;
}

void typio_wl_key_debug_format(uint32_t unicode, char *buffer, size_t size) {
    char utf8[5];

    if (!buffer || size == 0)
        return;

    if (unicode == 0) {
        snprintf(buffer, size, "unicode=none char=-");
        return;
    }

    switch (unicode) {
    case '\b':
        snprintf(buffer, size, "unicode=U+%04X char='\\\\b'", unicode);
        return;
    case '\t':
        snprintf(buffer, size, "unicode=U+%04X char='\\\\t'", unicode);
        return;
    case '\n':
        snprintf(buffer, size, "unicode=U+%04X char='\\\\n'", unicode);
        return;
    case '\r':
        snprintf(buffer, size, "unicode=U+%04X char='\\\\r'", unicode);
        return;
    case '\\':
        snprintf(buffer, size, "unicode=U+%04X char='\\\\\\\\'", unicode);
        return;
    case '\'':
        snprintf(buffer, size, "unicode=U+%04X char='\\\\''", unicode);
        return;
    default:
        break;
    }

    if (unicode < 0x20 || (unicode >= 0x7f && unicode < 0xa0) ||
        key_debug_encode_utf8(unicode, utf8) == 0) {
        snprintf(buffer, size, "unicode=U+%04X char=U+%04X", unicode, unicode);
        return;
    }

    snprintf(buffer, size, "unicode=U+%04X char='%s'", unicode, utf8);
}

void typio_wl_key_debug_format_keysym(uint32_t keysym, char *buffer, size_t size) {
    char name[64];
    int len;

    if (!buffer || size == 0)
        return;

    len = xkb_keysym_get_name((xkb_keysym_t)keysym, name, sizeof(name));
    if (len <= 0) {
        snprintf(buffer, size, "keyname=-");
        return;
    }

    snprintf(buffer, size, "keyname=%s", name);
}
