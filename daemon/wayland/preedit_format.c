/**
 * @file preedit_format.c
 * @brief Formatting helpers for plain preedit display
 */

#include "preedit_format.h"

#include <stdlib.h>
#include <string.h>

static bool append_text(char **buffer, size_t *length, size_t *capacity,
                        const char *text) {
    size_t text_len;
    size_t needed;
    char *resized;

    if (!buffer || !length || !capacity || !text || !*text) {
        return true;
    }

    text_len = strlen(text);
    needed = *length + text_len + 1;
    if (needed > *capacity) {
        size_t new_capacity = *capacity ? *capacity : 64;
        while (new_capacity < needed) {
            new_capacity *= 2;
        }

        resized = realloc(*buffer, new_capacity);
        if (!resized) {
            return false;
        }

        *buffer = resized;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *length, text, text_len);
    *length += text_len;
    (*buffer)[*length] = '\0';
    return true;
}

char *typio_wl_build_plain_preedit(const TypioPreedit *preedit, int *cursor_pos) {
    char *buffer = nullptr;
    size_t length = 0;
    size_t capacity = 0;

    if (cursor_pos) {
        *cursor_pos = -1;
    }

    if (!preedit || preedit->segment_count == 0) {
        return nullptr;
    }

    for (size_t i = 0; i < preedit->segment_count; ++i) {
        if (preedit->segments[i].text &&
            !append_text(&buffer, &length, &capacity, preedit->segments[i].text)) {
            free(buffer);
            return nullptr;
        }
    }

    if (cursor_pos) {
        *cursor_pos = preedit->cursor_pos >= 0 ? preedit->cursor_pos : (int)length;
    }

    return buffer;
}
