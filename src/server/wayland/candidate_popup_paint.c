/**
 * @file candidate_popup_paint.c
 * @brief Cairo paint-and-commit path for the popup UI
 */

#include "candidate_popup_paint.h"

#include <cairo.h>
#include <pango/pangocairo.h>
#include <wayland-client.h>

#include <limits.h>
#include <string.h>

#define TYPIO_CANDIDATE_POPUP_PADDING 8
#define TYPIO_CANDIDATE_POPUP_ROW_PADDING_X 4
#define TYPIO_CANDIDATE_POPUP_ROW_PADDING_Y 2

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

static void popup_draw_text(cairo_t *cr, PangoFontDescription *font,
                            const char *text,
                            double x, double y,
                            double r, double g, double b) {
    PangoLayout *layout = pango_cairo_create_layout(cr);

    if (!layout) {
        return;
    }

    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text ? text : "", -1);

    cairo_move_to(cr, x, y);
    cairo_set_source_rgb(cr, r, g, b);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
}

bool typio_candidate_popup_paint_and_commit(const TypioCandidatePopupPaintTarget *target,
                                  TypioCandidatePopupFontCache *font_cache,
                                  const TypioCandidatePopupLine *lines,
                                  size_t line_count,
                                  int selected,
                                  const char *preedit_text,
                                  int width, int height,
                                  int scale,
                                  const TypioCandidatePopupRenderConfig *config,
                                  const TypioCandidatePopupPalette *palette) {
    int buffer_width;
    int buffer_height;
    TypioCandidatePopupBuffer *buffer;
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoFontDescription *font;
    PangoFontDescription *page_font;

    if (!target || !target->surface || !target->shm || !target->buffers ||
        !font_cache || !config || !palette) {
        return false;
    }

    if (!popup_scaled_dimension(width, scale, &buffer_width) ||
        !popup_scaled_dimension(height, scale, &buffer_height)) {
        return false;
    }

    buffer = typio_candidate_popup_buffer_acquire(target->buffers, target->buffer_count,
                                        target->shm, buffer_width, buffer_height);
    if (!buffer) {
        return false;
    }

    memset(buffer->data, 0, buffer->size);
    surface = cairo_image_surface_create_for_data((unsigned char *)buffer->data,
                                                  CAIRO_FORMAT_ARGB32,
                                                  buffer_width, buffer_height,
                                                  buffer->stride);
    cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);

    cairo_set_source_rgba(cr, palette->bg_r, palette->bg_g,
                          palette->bg_b, palette->bg_a);
    cairo_paint(cr);

    cairo_set_source_rgba(cr, palette->border_r, palette->border_g,
                          palette->border_b, palette->border_a);
    cairo_rectangle(cr, 0.5, 0.5, width - 1.0, height - 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    font = typio_candidate_popup_font_get(font_cache, config->font_desc, false);
    page_font = typio_candidate_popup_font_get(font_cache, config->page_font_desc, true);

    if (preedit_text) {
        popup_draw_text(cr, page_font, preedit_text,
                        TYPIO_CANDIDATE_POPUP_PADDING, TYPIO_CANDIDATE_POPUP_PADDING,
                        palette->preedit_r, palette->preedit_g, palette->preedit_b);
    }

    for (size_t i = 0; i < line_count; ++i) {
        const TypioCandidatePopupLine *line = &lines[i];
        bool is_selected = (selected >= 0 && (size_t)selected == i);

        if (is_selected) {
            cairo_set_source_rgba(cr, palette->selection_r, palette->selection_g,
                                  palette->selection_b, palette->selection_a);
            cairo_rectangle(cr, line->x, line->y, line->width, line->height);
            cairo_fill(cr);
            popup_draw_text(cr, font, line->text,
                            line->x + TYPIO_CANDIDATE_POPUP_ROW_PADDING_X,
                            line->y + TYPIO_CANDIDATE_POPUP_ROW_PADDING_Y,
                            palette->selection_text_r,
                            palette->selection_text_g,
                            palette->selection_text_b);
            continue;
        }

        popup_draw_text(cr, font, line->text,
                        line->x + TYPIO_CANDIDATE_POPUP_ROW_PADDING_X,
                        line->y + TYPIO_CANDIDATE_POPUP_ROW_PADDING_Y,
                        palette->text_r, palette->text_g, palette->text_b);
    }

    cairo_surface_flush(surface);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    wl_surface_set_buffer_scale(target->surface, scale);
    wl_surface_attach(target->surface, buffer->buffer, 0, 0);
    wl_surface_damage(target->surface, 0, 0, width, height);
    wl_surface_commit(target->surface);
    buffer->busy = true;
    return true;
}
