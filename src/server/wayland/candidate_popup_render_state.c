#include "candidate_popup_render_state.h"

#include <limits.h>
#include <string.h>

static bool popup_scaled_dimension(int logical, int scale, int *physical) {
    if (!physical || logical < 0 || scale < 1) {
        return false;
    }

    if (logical > INT_MAX / scale) {
        return false;
    }

    *physical = logical * scale;
    return true;
}

bool typio_candidate_popup_render_state_matches(const TypioCandidatePopupRenderState *cached,
                                      const TypioCandidatePopupRenderState *current,
                                      int scale) {
    int buffer_w;
    int buffer_h;
    const char *old_pre;
    const char *new_pre;

    if (!cached || !current || !cached->cache_valid ||
        cached->line_count != current->line_count) {
        return false;
    }

    if (cached->content_signature != current->content_signature ||
        cached->palette_token != current->palette_token ||
        cached->theme_mode != current->theme_mode ||
        cached->layout_mode != current->layout_mode ||
        cached->font_size != current->font_size ||
        strcmp(cached->font_desc ? cached->font_desc : "",
               current->font_desc ? current->font_desc : "") != 0 ||
        strcmp(cached->page_font_desc ? cached->page_font_desc : "",
               current->page_font_desc ? current->page_font_desc : "") != 0) {
        return false;
    }

    if (!popup_scaled_dimension(cached->width, scale, &buffer_w) ||
        !popup_scaled_dimension(cached->height, scale, &buffer_h)) {
        return false;
    }

    old_pre = cached->preedit_text ? cached->preedit_text : "";
    new_pre = current->preedit_text ? current->preedit_text : "";
    if (strcmp(old_pre, new_pre) != 0) {
        return false;
    }

    return true;
}
