#ifndef TYPIO_WL_POPUP_RENDER_STATE_H
#define TYPIO_WL_POPUP_RENDER_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct TypioPopupRenderState {
    bool cache_valid;
    size_t line_count;
    uint64_t content_signature;
    const void *palette_token;
    int theme_mode;
    int layout_mode;
    int font_size;
    const char *font_desc;
    const char *page_font_desc;
    int width;
    int height;
    const char *preedit_text;
} TypioPopupRenderState;

bool typio_popup_render_state_matches(const TypioPopupRenderState *cached,
                                      const TypioPopupRenderState *current,
                                      int scale);

#endif /* TYPIO_WL_POPUP_RENDER_STATE_H */
