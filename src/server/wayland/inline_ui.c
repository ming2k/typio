/**
 * @file inline_ui.c
 * @brief Formatting helpers for inline preedit and candidate display
 */

#include "inline_ui.h"

#include <stdbool.h>
#include <stdio.h>
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

static bool append_candidate(char **buffer, size_t *length, size_t *capacity,
                             const TypioCandidate *candidate, size_t index,
                             bool selected) {
    char fallback_label[32];

    if (selected && !append_text(buffer, length, capacity, "[")) {
        return false;
    }

    if (candidate && candidate->label && *candidate->label) {
        if (!append_text(buffer, length, capacity, candidate->label)) {
            return false;
        }
    } else {
        snprintf(fallback_label, sizeof(fallback_label), "%zu", index + 1);
        if (!append_text(buffer, length, capacity, fallback_label)) {
            return false;
        }
    }

    if (!append_text(buffer, length, capacity, ". ")) {
        return false;
    }
    if (candidate && candidate->text && !append_text(buffer, length, capacity, candidate->text)) {
        return false;
    }
    if (candidate && candidate->comment && *candidate->comment) {
        if (!append_text(buffer, length, capacity, " ")) {
            return false;
        }
        if (!append_text(buffer, length, capacity, candidate->comment)) {
            return false;
        }
    }

    if (selected && !append_text(buffer, length, capacity, "]")) {
        return false;
    }

    return true;
}

char *typio_wl_build_plain_preedit(const TypioPreedit *preedit, int *cursor_pos) {
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;

    if (cursor_pos) {
        *cursor_pos = -1;
    }

    if (!preedit || preedit->segment_count == 0) {
        return NULL;
    }

    for (size_t i = 0; i < preedit->segment_count; ++i) {
        if (preedit->segments[i].text &&
            !append_text(&buffer, &length, &capacity, preedit->segments[i].text)) {
            free(buffer);
            return NULL;
        }
    }

    if (cursor_pos) {
        *cursor_pos = preedit->cursor_pos >= 0 ? preedit->cursor_pos : (int)length;
    }

    return buffer;
}

char *typio_wl_build_inline_preedit(const TypioPreedit *preedit,
                                    const TypioCandidateList *candidates,
                                    int *cursor_pos) {
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    bool has_preedit = preedit && preedit->segment_count > 0;
    bool has_candidates = candidates && candidates->count > 0;

    if (cursor_pos) {
        *cursor_pos = -1;
    }

    if (!has_preedit && !has_candidates) {
        return NULL;
    }

    if (has_preedit) {
        buffer = typio_wl_build_plain_preedit(preedit, cursor_pos);
        if (!buffer) {
            return NULL;
        }
        length = strlen(buffer);
        capacity = length + 1;
    }

    if (has_candidates) {
        if (length > 0 && !append_text(&buffer, &length, &capacity, "  ")) {
            free(buffer);
            return NULL;
        }

        for (size_t i = 0; i < candidates->count; ++i) {
            bool selected = candidates->selected >= 0 &&
                            (size_t)candidates->selected == i;

            if (i > 0 && !append_text(&buffer, &length, &capacity, " ")) {
                free(buffer);
                return NULL;
            }

            if (!append_candidate(&buffer, &length, &capacity,
                                  &candidates->candidates[i], i, selected)) {
                free(buffer);
                return NULL;
            }
        }

        if (candidates->has_prev && !append_text(&buffer, &length, &capacity, " <")) {
            free(buffer);
            return NULL;
        }
        if (candidates->has_next && !append_text(&buffer, &length, &capacity, " >")) {
            free(buffer);
            return NULL;
        }
    }

    return buffer;
}
