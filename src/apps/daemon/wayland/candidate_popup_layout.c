/**
 * @file candidate_popup_layout.c
 * @brief Layout computation and cache for candidate popup UI
 *
 * Handles text measurement with Pango, candidate formatting, position
 * computation for vertical/horizontal layout modes, and the layout cache
 * that allows selection-only changes to skip remeasurement.
 */

#include "candidate_popup_layout.h"
#include "candidate_popup_render_state.h"

#include <cairo.h>
#include <pango/pangocairo.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TYPIO_CANDIDATE_POPUP_PADDING 8
#define TYPIO_CANDIDATE_POPUP_ROW_GAP 4
#define TYPIO_CANDIDATE_POPUP_COLUMN_GAP 10
#define TYPIO_CANDIDATE_POPUP_SECTION_GAP 6
#define TYPIO_CANDIDATE_POPUP_MIN_WIDTH 220
#define TYPIO_CANDIDATE_POPUP_ROW_PADDING_X 4
#define TYPIO_CANDIDATE_POPUP_ROW_PADDING_Y 2

/* ── Candidate formatting ──────────────────────────────────────────── */

static char *format_candidate(const TypioCandidate *candidate, size_t index) {
    const char *label;
    const char *text;
    char fallback_label[32];
    char buf[512];

    if (!candidate || !candidate->text) {
        return nullptr;
    }

    text = candidate->text;

    if (candidate->label && candidate->label[0]) {
        label = candidate->label;
    } else {
        snprintf(fallback_label, sizeof(fallback_label), "%zu", index + 1);
        label = fallback_label;
    }

    if (candidate->comment && candidate->comment[0]) {
        snprintf(buf, sizeof(buf), "%s. %s  %s", label, text, candidate->comment);
    } else {
        snprintf(buf, sizeof(buf), "%s. %s", label, text);
    }

    return strdup(buf);
}

/* ── Line helpers ──────────────────────────────────────────────────── */

static void free_lines(TypioCandidatePopupLine *lines, size_t count) {
    if (!lines) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        if (lines[i].layout) {
            g_object_unref(lines[i].layout);
        }
        free(lines[i].text);
    }
    free(lines);
}

/* ── Text measurement ──────────────────────────────────────────────── */

static PangoLayout *create_text_layout(cairo_t *cr, PangoFontDescription *font,
                                       const char *text) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    if (!layout) {
        return nullptr;
    }
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text ? text : "", -1);
    return layout;
}

static bool measure_layout(PangoLayout *layout, int *width, int *height) {
    if (!layout) {
        return false;
    }
    pango_layout_get_pixel_size(layout, width, height);
    return true;
}

/* ── Font cache ────────────────────────────────────────────────────── */

PangoFontDescription *typio_candidate_popup_font_get(TypioCandidatePopupFontCache *fc,
                                           const char *font_desc,
                                           bool is_page_font) {
    char *cached_desc = is_page_font ? fc->page_font_desc : fc->font_desc;
    PangoFontDescription **cached_font = is_page_font ? &fc->page_font : &fc->font;

    if (*cached_font && strcmp(cached_desc, font_desc) == 0) {
        return *cached_font;
    }

    if (*cached_font) {
        pango_font_description_free(*cached_font);
    }

    *cached_font = pango_font_description_from_string(font_desc);
    snprintf(is_page_font ? fc->page_font_desc : fc->font_desc,
             sizeof(fc->font_desc), "%s", font_desc);
    return *cached_font;
}

void typio_candidate_popup_font_cache_free(TypioCandidatePopupFontCache *fc) {
    if (!fc) {
        return;
    }
    if (fc->font) {
        pango_font_description_free(fc->font);
        fc->font = nullptr;
    }
    if (fc->page_font) {
        pango_font_description_free(fc->page_font);
        fc->page_font = nullptr;
    }
}

/* ── Cache management ──────────────────────────────────────────────── */

void typio_candidate_popup_layout_cache_invalidate(TypioCandidatePopupCache *cache) {
    if (!cache) {
        return;
    }
    free_lines(cache->lines, cache->line_count);
    free(cache->preedit_text);
    if (cache->preedit_layout) {
        g_object_unref(cache->preedit_layout);
    }
    free(cache->mode_label);
    if (cache->mode_label_layout) {
        g_object_unref(cache->mode_label_layout);
    }
    memset(cache, 0, sizeof(*cache));
    cache->selected = -1;
}

void typio_candidate_popup_layout_cache_update_aux(TypioCandidatePopupCache *cache,
                                         char *preedit_text,
                                         PangoLayout *preedit_layout,
                                         int preedit_width, int preedit_height,
                                         int preedit_x, int preedit_y,
                                         char *mode_label,
                                         PangoLayout *mode_label_layout,
                                         int mode_label_width, int mode_label_height,
                                         int mode_label_x, int mode_label_y,
                                         int mode_label_divider_y) {
    if (!cache) {
        return;
    }

    free(cache->preedit_text);
    if (cache->preedit_layout) {
        g_object_unref(cache->preedit_layout);
    }
    free(cache->mode_label);
    if (cache->mode_label_layout) {
        g_object_unref(cache->mode_label_layout);
    }

    cache->preedit_text = preedit_text;
    cache->preedit_layout = preedit_layout;
    cache->preedit_width = preedit_width;
    cache->preedit_height = preedit_height;
    cache->preedit_x = preedit_x;
    cache->preedit_y = preedit_y;
    cache->mode_label = mode_label;
    cache->mode_label_layout = mode_label_layout;
    cache->mode_label_width = mode_label_width;
    cache->mode_label_height = mode_label_height;
    cache->mode_label_x = mode_label_x;
    cache->mode_label_y = mode_label_y;
    cache->mode_label_divider_y = mode_label_divider_y;
}

void typio_candidate_popup_layout_cache_store(TypioCandidatePopupCache *cache,
                                    TypioCandidatePopupLine *lines, size_t line_count,
                                    int selected,
                                    uint64_t content_signature,
                                    char *preedit_text,
                                    PangoLayout *preedit_layout,
                                    int preedit_width, int preedit_height,
                                    int preedit_x, int preedit_y,
                                    char *mode_label,
                                    PangoLayout *mode_label_layout,
                                    int mode_label_width, int mode_label_height,
                                    int mode_label_x, int mode_label_y,
                                    int mode_label_divider_y,
                                    int width, int height,
                                    const TypioCandidatePopupRenderConfig *config,
                                    const TypioCandidatePopupPalette *palette) {
    free_lines(cache->lines, cache->line_count);
    free(cache->preedit_text);
    if (cache->preedit_layout) {
        g_object_unref(cache->preedit_layout);
    }
    free(cache->mode_label);
    if (cache->mode_label_layout) {
        g_object_unref(cache->mode_label_layout);
    }
    cache->preedit_text = nullptr;
    cache->preedit_layout = nullptr;
    cache->mode_label = nullptr;
    cache->mode_label_layout = nullptr;

    cache->lines = lines;
    cache->line_count = line_count;
    cache->selected = selected;
    cache->content_signature = content_signature;
    cache->width = width;
    cache->height = height;
    cache->config = *config;
    cache->palette = palette;
    cache->valid = true;
    typio_candidate_popup_layout_cache_update_aux(cache,
                                         preedit_text,
                                         preedit_layout,
                                         preedit_width, preedit_height,
                                         preedit_x, preedit_y,
                                         mode_label,
                                         mode_label_layout,
                                         mode_label_width, mode_label_height,
                                         mode_label_x, mode_label_y,
                                         mode_label_divider_y);
}

bool typio_candidate_popup_layout_cache_matches(const TypioCandidatePopupCache *cache,
                                      const TypioCandidateList *candidates,
                                      const char *preedit_text,
                                      const char *mode_label,
                                      int scale,
                                      const TypioCandidatePopupRenderConfig *config,
                                      const TypioCandidatePopupPalette *palette) {
    TypioCandidatePopupRenderState cached = {
        .cache_valid = cache->valid,
        .line_count = cache->line_count,
        .content_signature = cache->content_signature,
        .palette_token = cache->palette,
        .theme_mode = cache->config.theme_mode,
        .layout_mode = cache->config.layout_mode,
        .font_size = cache->config.font_size,
        .font_desc = cache->config.font_desc,
        .page_font_desc = cache->config.page_font_desc,
        .width = cache->width,
        .height = cache->height,
        .preedit_text = cache->preedit_text,
        .mode_label = cache->mode_label,
    };
    TypioCandidatePopupRenderState current = {
        .cache_valid = true,
        .line_count = candidates->count,
        .content_signature = candidates->content_signature,
        .palette_token = palette,
        .theme_mode = config->theme_mode,
        .layout_mode = config->layout_mode,
        .font_size = config->font_size,
        .font_desc = config->font_desc,
        .page_font_desc = config->page_font_desc,
        .width = cache->width,
        .height = cache->height,
        .preedit_text = preedit_text,
        .mode_label = mode_label,
    };

    return typio_candidate_popup_render_state_matches(&cached, &current, scale);
}

/* ── Layout computation ────────────────────────────────────────────── */

bool typio_candidate_popup_layout_compute(const TypioCandidateList *candidates,
                                const char *preedit_text_in,
                                const char *mode_label_in,
                                const TypioCandidatePopupRenderConfig *config,
                                TypioCandidatePopupFontCache *font_cache,
                                TypioCandidatePopupLine **out_lines,
                                size_t *out_line_count,
                                PangoLayout **out_preedit_layout,
                                int *out_preedit_width,
                                int *out_preedit_height,
                                int *out_preedit_x,
                                int *out_preedit_y,
                                PangoLayout **out_mode_label_layout,
                                int *out_mode_label_width,
                                int *out_mode_label_height,
                                int *out_mode_label_x,
                                int *out_mode_label_y,
                                int *out_mode_label_divider_y,
                                int *out_width,
                                int *out_height) {
    size_t line_count = candidates->count;
    TypioCandidatePopupLine *lines = calloc(line_count, sizeof(*lines));
    PangoLayout *preedit_layout = nullptr;
    PangoLayout *mode_label_layout = nullptr;
    if (!lines) {
        return false;
    }

    PangoFontDescription *font = typio_candidate_popup_font_get(
        font_cache, config->font_desc, false);
    PangoFontDescription *page_font = typio_candidate_popup_font_get(
        font_cache, config->page_font_desc, true);

    cairo_surface_t *scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(scratch);

    int preedit_width = 0;
    int preedit_height = 0;
    if (preedit_text_in) {
        preedit_layout = create_text_layout(cr, page_font, preedit_text_in);
    }
    if (preedit_text_in && !measure_layout(preedit_layout, &preedit_width, &preedit_height)) {
        cairo_destroy(cr);
        cairo_surface_destroy(scratch);
        free_lines(lines, line_count);
        if (preedit_layout) {
            g_object_unref(preedit_layout);
        }
        return false;
    }

    int mode_label_width = 0;
    int mode_label_height = 0;
    if (mode_label_in) {
        mode_label_layout = create_text_layout(cr, page_font, mode_label_in);
    }
    if (mode_label_in &&
        !measure_layout(mode_label_layout, &mode_label_width, &mode_label_height)) {
        cairo_destroy(cr);
        cairo_surface_destroy(scratch);
        free_lines(lines, line_count);
        if (preedit_layout) {
            g_object_unref(preedit_layout);
        }
        if (mode_label_layout) {
            g_object_unref(mode_label_layout);
        }
        return false;
    }

    int content_width = preedit_width;
    int content_height = preedit_text_in ? preedit_height : 0;
    int items_height = 0;
    int row_width = 0;
    int row_height = 0;

    for (size_t i = 0; i < line_count; ++i) {
        lines[i].text = format_candidate(&candidates->candidates[i], i);
        if (lines[i].text) {
            lines[i].layout = create_text_layout(cr, font, lines[i].text);
        }
        if (!lines[i].text || !measure_layout(lines[i].layout,
                          &lines[i].text_width, &lines[i].text_height)) {
            cairo_destroy(cr);
            cairo_surface_destroy(scratch);
            free_lines(lines, line_count);
            if (preedit_layout) {
                g_object_unref(preedit_layout);
            }
            if (mode_label_layout) {
                g_object_unref(mode_label_layout);
            }
            return false;
        }

        lines[i].width = lines[i].text_width + TYPIO_CANDIDATE_POPUP_ROW_PADDING_X * 2;
        lines[i].height = lines[i].text_height + TYPIO_CANDIDATE_POPUP_ROW_PADDING_Y * 2;

        if (config->layout_mode == TYPIO_CANDIDATE_POPUP_LAYOUT_VERTICAL) {
            if (lines[i].width > content_width) {
                content_width = lines[i].width;
            }
            items_height += lines[i].height;
            if (i + 1 < line_count) {
                items_height += TYPIO_CANDIDATE_POPUP_ROW_GAP;
            }
        } else {
            if (row_width > 0) {
                row_width += TYPIO_CANDIDATE_POPUP_COLUMN_GAP;
            }
            row_width += lines[i].width;
            if (lines[i].height > row_height) {
                row_height = lines[i].height;
            }
        }
    }

    cairo_destroy(cr);
    cairo_surface_destroy(scratch);

    if (config->layout_mode == TYPIO_CANDIDATE_POPUP_LAYOUT_HORIZONTAL) {
        if (row_height > 0) {
            items_height += row_height;
        }
        int total_row = row_width;
        if (mode_label_in && mode_label_width > 0) {
            total_row += TYPIO_CANDIDATE_POPUP_COLUMN_GAP + mode_label_width;
        }
        if (total_row > content_width) {
            content_width = total_row;
        }
    }

    if (preedit_text_in && line_count > 0) {
        content_height += TYPIO_CANDIDATE_POPUP_SECTION_GAP;
    }
    content_height += items_height;

    if (mode_label_in && mode_label_height > 0 &&
        config->layout_mode == TYPIO_CANDIDATE_POPUP_LAYOUT_VERTICAL) {
        content_height += TYPIO_CANDIDATE_POPUP_SECTION_GAP + mode_label_height;
        if (mode_label_width > content_width) {
            content_width = mode_label_width;
        }
    }

    int width = content_width + TYPIO_CANDIDATE_POPUP_PADDING * 2;
    if (width < TYPIO_CANDIDATE_POPUP_MIN_WIDTH) {
        width = TYPIO_CANDIDATE_POPUP_MIN_WIDTH;
    }
    int height = content_height + TYPIO_CANDIDATE_POPUP_PADDING * 2;

    /* Compute row positions. */
    int y = TYPIO_CANDIDATE_POPUP_PADDING;
    int preedit_x = TYPIO_CANDIDATE_POPUP_PADDING;
    int preedit_y = TYPIO_CANDIDATE_POPUP_PADDING;
    if (preedit_text_in) {
        y += preedit_height + TYPIO_CANDIDATE_POPUP_SECTION_GAP;
    }

    if (config->layout_mode == TYPIO_CANDIDATE_POPUP_LAYOUT_VERTICAL) {
        for (size_t i = 0; i < line_count; ++i) {
            lines[i].x = TYPIO_CANDIDATE_POPUP_PADDING;
            lines[i].y = y;
            lines[i].width = width - TYPIO_CANDIDATE_POPUP_PADDING * 2;
            lines[i].text_x = lines[i].x + TYPIO_CANDIDATE_POPUP_ROW_PADDING_X;
            lines[i].text_y = lines[i].y + TYPIO_CANDIDATE_POPUP_ROW_PADDING_Y;
            y += lines[i].height;
            if (i + 1 < line_count) {
                y += TYPIO_CANDIDATE_POPUP_ROW_GAP;
            }
        }
    } else {
        int x = TYPIO_CANDIDATE_POPUP_PADDING;
        for (size_t i = 0; i < line_count; ++i) {
            lines[i].x = x;
            lines[i].y = y;
            lines[i].text_x = lines[i].x + TYPIO_CANDIDATE_POPUP_ROW_PADDING_X;
            lines[i].text_y = lines[i].y + TYPIO_CANDIDATE_POPUP_ROW_PADDING_Y;
            x += lines[i].width + TYPIO_CANDIDATE_POPUP_COLUMN_GAP;
        }
    }

    int mode_label_x = 0;
    int mode_label_y = 0;
    int mode_label_divider_y = -1;
    if (mode_label_in && mode_label_height > 0) {
        mode_label_x = width - TYPIO_CANDIDATE_POPUP_PADDING - mode_label_width;
        if (config->layout_mode == TYPIO_CANDIDATE_POPUP_LAYOUT_HORIZONTAL) {
            mode_label_y = y + TYPIO_CANDIDATE_POPUP_ROW_PADDING_Y;
        } else {
            mode_label_divider_y = height - TYPIO_CANDIDATE_POPUP_PADDING -
                                   mode_label_height - TYPIO_CANDIDATE_POPUP_ROW_PADDING_Y;
            mode_label_y = height - TYPIO_CANDIDATE_POPUP_PADDING - mode_label_height;
        }
    }

    *out_lines = lines;
    *out_line_count = line_count;
    *out_preedit_layout = preedit_layout;
    *out_preedit_width = preedit_width;
    *out_preedit_height = preedit_height;
    *out_preedit_x = preedit_x;
    *out_preedit_y = preedit_y;
    *out_mode_label_layout = mode_label_layout;
    *out_mode_label_width = mode_label_width;
    *out_mode_label_height = mode_label_height;
    *out_mode_label_x = mode_label_x;
    *out_mode_label_y = mode_label_y;
    *out_mode_label_divider_y = mode_label_divider_y;
    *out_width = width;
    *out_height = height;
    return true;
}

bool typio_candidate_popup_layout_measure_aux(const TypioCandidatePopupCache *cache,
                                    const char *preedit_text_in,
                                    const char *mode_label_in,
                                    const TypioCandidatePopupRenderConfig *config,
                                    TypioCandidatePopupFontCache *font_cache,
                                    PangoLayout **out_preedit_layout,
                                    int *out_preedit_width,
                                    int *out_preedit_height,
                                    int *out_preedit_x,
                                    int *out_preedit_y,
                                    PangoLayout **out_mode_label_layout,
                                    int *out_mode_label_width,
                                    int *out_mode_label_height,
                                    int *out_mode_label_x,
                                    int *out_mode_label_y,
                                    int *out_mode_label_divider_y,
                                    int *out_width,
                                    int *out_height) {
    PangoFontDescription *page_font;
    cairo_surface_t *scratch;
    cairo_t *cr;
    PangoLayout *preedit_layout = nullptr;
    PangoLayout *mode_label_layout = nullptr;
    int preedit_width = 0;
    int preedit_height = 0;
    int mode_label_width = 0;
    int mode_label_height = 0;
    int items_height = 0;
    int content_width = 0;
    int content_height = 0;
    int row_width = 0;
    int row_height = 0;
    int preedit_x = TYPIO_CANDIDATE_POPUP_PADDING;
    int preedit_y = TYPIO_CANDIDATE_POPUP_PADDING;
    int mode_label_x = 0;
    int mode_label_y = 0;
    int mode_label_divider_y = -1;
    int width;
    int height;
    int y;

    if (!cache || !cache->valid || !config || !font_cache) {
        return false;
    }

    page_font = typio_candidate_popup_font_get(font_cache, config->page_font_desc, true);
    scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cr = cairo_create(scratch);

    if (preedit_text_in) {
        preedit_layout = create_text_layout(cr, page_font, preedit_text_in);
    }
    if (preedit_text_in && !measure_layout(preedit_layout, &preedit_width, &preedit_height)) {
        cairo_destroy(cr);
        cairo_surface_destroy(scratch);
        if (preedit_layout) {
            g_object_unref(preedit_layout);
        }
        return false;
    }

    if (mode_label_in) {
        mode_label_layout = create_text_layout(cr, page_font, mode_label_in);
    }
    if (mode_label_in &&
        !measure_layout(mode_label_layout, &mode_label_width, &mode_label_height)) {
        cairo_destroy(cr);
        cairo_surface_destroy(scratch);
        if (preedit_layout) {
            g_object_unref(preedit_layout);
        }
        if (mode_label_layout) {
            g_object_unref(mode_label_layout);
        }
        return false;
    }

    cairo_destroy(cr);
    cairo_surface_destroy(scratch);

    content_width = preedit_width;
    content_height = preedit_text_in ? preedit_height : 0;

    if (config->layout_mode == TYPIO_CANDIDATE_POPUP_LAYOUT_VERTICAL) {
        for (size_t i = 0; i < cache->line_count; ++i) {
            if (cache->lines[i].width > content_width) {
                content_width = cache->lines[i].width;
            }
            items_height += cache->lines[i].height;
            if (i + 1 < cache->line_count) {
                items_height += TYPIO_CANDIDATE_POPUP_ROW_GAP;
            }
        }
    } else {
        for (size_t i = 0; i < cache->line_count; ++i) {
            if (row_width > 0) {
                row_width += TYPIO_CANDIDATE_POPUP_COLUMN_GAP;
            }
            row_width += cache->lines[i].width;
            if (cache->lines[i].height > row_height) {
                row_height = cache->lines[i].height;
            }
        }
        if (row_height > 0) {
            items_height = row_height;
        }
        content_width = row_width;
        if (preedit_width > content_width) {
            content_width = preedit_width;
        }
        if (mode_label_in && mode_label_width > 0) {
            int total_row = row_width + TYPIO_CANDIDATE_POPUP_COLUMN_GAP + mode_label_width;
            if (total_row > content_width) {
                content_width = total_row;
            }
        }
    }

    if (preedit_text_in && cache->line_count > 0) {
        content_height += TYPIO_CANDIDATE_POPUP_SECTION_GAP;
    }
    content_height += items_height;

    if (mode_label_in && mode_label_height > 0 &&
        config->layout_mode == TYPIO_CANDIDATE_POPUP_LAYOUT_VERTICAL) {
        content_height += TYPIO_CANDIDATE_POPUP_SECTION_GAP + mode_label_height;
        if (mode_label_width > content_width) {
            content_width = mode_label_width;
        }
    }

    width = content_width + TYPIO_CANDIDATE_POPUP_PADDING * 2;
    if (width < TYPIO_CANDIDATE_POPUP_MIN_WIDTH) {
        width = TYPIO_CANDIDATE_POPUP_MIN_WIDTH;
    }
    height = content_height + TYPIO_CANDIDATE_POPUP_PADDING * 2;

    y = TYPIO_CANDIDATE_POPUP_PADDING;
    if (preedit_text_in) {
        y += preedit_height + TYPIO_CANDIDATE_POPUP_SECTION_GAP;
    }

    if (mode_label_in && mode_label_height > 0) {
        mode_label_x = width - TYPIO_CANDIDATE_POPUP_PADDING - mode_label_width;
        if (config->layout_mode == TYPIO_CANDIDATE_POPUP_LAYOUT_HORIZONTAL) {
            mode_label_y = y + TYPIO_CANDIDATE_POPUP_ROW_PADDING_Y;
        } else {
            mode_label_divider_y = height - TYPIO_CANDIDATE_POPUP_PADDING -
                                   mode_label_height - TYPIO_CANDIDATE_POPUP_ROW_PADDING_Y;
            mode_label_y = height - TYPIO_CANDIDATE_POPUP_PADDING - mode_label_height;
        }
    }

    *out_preedit_layout = preedit_layout;
    *out_preedit_width = preedit_width;
    *out_preedit_height = preedit_height;
    *out_preedit_x = preedit_x;
    *out_preedit_y = preedit_y;
    *out_mode_label_layout = mode_label_layout;
    *out_mode_label_width = mode_label_width;
    *out_mode_label_height = mode_label_height;
    *out_mode_label_x = mode_label_x;
    *out_mode_label_y = mode_label_y;
    *out_mode_label_divider_y = mode_label_divider_y;
    *out_width = width;
    *out_height = height;
    return true;
}
