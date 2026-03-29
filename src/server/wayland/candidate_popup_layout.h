/**
 * @file candidate_popup_layout.h
 * @brief Layout computation and cache for candidate popup UI
 */

#ifndef TYPIO_WL_CANDIDATE_POPUP_LAYOUT_H
#define TYPIO_WL_CANDIDATE_POPUP_LAYOUT_H

#include "candidate_popup_theme.h"
#include "typio/input_context.h"

#include <pango/pango.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum TypioCandidatePopupLayoutMode {
    TYPIO_CANDIDATE_POPUP_LAYOUT_HORIZONTAL = 0,
    TYPIO_CANDIDATE_POPUP_LAYOUT_VERTICAL,
} TypioCandidatePopupLayoutMode;

typedef struct TypioCandidatePopupRenderConfig {
    TypioCandidatePopupThemeMode theme_mode;
    TypioCandidatePopupLayoutMode layout_mode;
    int font_size;
    bool mode_indicator;
    char font_desc[64];
    char page_font_desc[64];
} TypioCandidatePopupRenderConfig;

typedef struct TypioCandidatePopupLine {
    char *text;
    int text_width;
    int text_height;
    int width;
    int height;
    int x;
    int y;
} TypioCandidatePopupLine;

typedef struct TypioCandidatePopupCache {
    TypioCandidatePopupLine *lines;
    size_t line_count;
    int selected;
    uint64_t content_signature;
    char *preedit_text;
    int preedit_width;
    int preedit_height;
    char *mode_label;
    int mode_label_width;
    int mode_label_height;
    int width;
    int height;
    TypioCandidatePopupRenderConfig config;
    const TypioCandidatePopupPalette *palette;
    bool valid;
} TypioCandidatePopupCache;

typedef struct TypioCandidatePopupFontCache {
    PangoFontDescription *font;
    PangoFontDescription *page_font;
    char font_desc[64];
    char page_font_desc[64];
} TypioCandidatePopupFontCache;

typedef struct TypioCandidatePopupConfigCache {
    TypioCandidatePopupRenderConfig render_config;
    bool valid;
} TypioCandidatePopupConfigCache;

void typio_candidate_popup_layout_cache_invalidate(TypioCandidatePopupCache *cache);

void typio_candidate_popup_layout_cache_store(TypioCandidatePopupCache *cache,
                                    TypioCandidatePopupLine *lines, size_t line_count,
                                    int selected,
                                    uint64_t content_signature,
                                    char *preedit_text,
                                    int preedit_width, int preedit_height,
                                    char *mode_label,
                                    int mode_label_width, int mode_label_height,
                                    int width, int height,
                                    const TypioCandidatePopupRenderConfig *config,
                                    const TypioCandidatePopupPalette *palette);

bool typio_candidate_popup_layout_cache_matches(const TypioCandidatePopupCache *cache,
                                      const TypioCandidateList *candidates,
                                      const char *preedit_text,
                                      const char *mode_label,
                                      int scale,
                                      const TypioCandidatePopupRenderConfig *config,
                                      const TypioCandidatePopupPalette *palette);

/**
 * Compute layout: measure text, build lines array, compute total dimensions.
 *
 * On success, *out_lines is heap-allocated and ownership passes to the
 * caller. Returns false on failure.
 */
bool typio_candidate_popup_layout_compute(const TypioCandidateList *candidates,
                                const char *preedit_text_in,
                                const char *mode_label_in,
                                const TypioCandidatePopupRenderConfig *config,
                                TypioCandidatePopupFontCache *font_cache,
                                TypioCandidatePopupLine **out_lines,
                                size_t *out_line_count,
                                int *out_preedit_width,
                                int *out_preedit_height,
                                int *out_mode_label_width,
                                int *out_mode_label_height,
                                int *out_width,
                                int *out_height);

PangoFontDescription *typio_candidate_popup_font_get(TypioCandidatePopupFontCache *fc,
                                           const char *font_desc,
                                           bool is_page_font);

void typio_candidate_popup_font_cache_free(TypioCandidatePopupFontCache *fc);

#endif /* TYPIO_WL_CANDIDATE_POPUP_LAYOUT_H */
