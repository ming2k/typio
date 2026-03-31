/**
 * @file candidate_popup_paint.c
 * @brief Cairo paint-and-commit path for the popup UI
 *
 * Full paint, selection-only repaint, and auxiliary-text-only repaint all land
 * here after layout has already determined geometry and cached text layouts.
 */

#include "candidate_popup_paint.h"
#include "candidate_popup_damage.h"

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

static void popup_draw_layout(cairo_t *cr, PangoLayout *layout,
                              double x, double y,
                              double r, double g, double b) {
    if (!layout) {
        return;
    }

    pango_cairo_update_layout(cr, layout);
    cairo_move_to(cr, x, y);
    cairo_set_source_rgb(cr, r, g, b);
    pango_cairo_show_layout(cr, layout);
}

static void popup_repaint_row(cairo_t *cr,
                              const TypioCandidatePopupLine *line,
                              bool is_selected,
                              const TypioCandidatePopupPalette *palette) {
    cairo_set_source_rgba(cr, palette->bg_r, palette->bg_g,
                          palette->bg_b, palette->bg_a);
    cairo_rectangle(cr, line->x, line->y, line->width, line->height);
    cairo_fill(cr);

    if (is_selected) {
        cairo_set_source_rgba(cr, palette->selection_r, palette->selection_g,
                              palette->selection_b, palette->selection_a);
        cairo_rectangle(cr, line->x, line->y, line->width, line->height);
        cairo_fill(cr);
        popup_draw_layout(cr, line->layout, line->text_x, line->text_y,
                        palette->selection_text_r,
                        palette->selection_text_g,
                        palette->selection_text_b);
    } else {
        popup_draw_layout(cr, line->layout, line->text_x, line->text_y,
                        palette->text_r, palette->text_g, palette->text_b);
    }
}

static void popup_clear_rect(cairo_t *cr,
                             const TypioCandidatePopupDamageLine *line,
                             const TypioCandidatePopupPalette *palette) {
    if (!line || line->width <= 0 || line->height <= 0) {
        return;
    }

    cairo_set_source_rgba(cr, palette->bg_r, palette->bg_g,
                          palette->bg_b, palette->bg_a);
    cairo_rectangle(cr, line->x, line->y, line->width, line->height);
    cairo_fill(cr);
}

static void popup_append_damage_line(TypioCandidatePopupDamageLine *lines,
                                     size_t *line_count,
                                     int x, int y, int width, int height) {
    if (!lines || !line_count || width <= 0 || height <= 0) {
        return;
    }

    lines[*line_count] = (TypioCandidatePopupDamageLine){
        .x = x,
        .y = y,
        .width = width,
        .height = height,
    };
    (*line_count)++;
}

bool typio_candidate_popup_paint_and_commit(const TypioCandidatePopupPaintTarget *target,
                                  const TypioCandidatePopupLine *lines,
                                  size_t line_count,
                                  int selected,
                                  PangoLayout *preedit_layout,
                                  int preedit_x,
                                  int preedit_y,
                                  PangoLayout *mode_label_layout,
                                  int mode_label_x,
                                  int mode_label_y,
                                  int mode_label_divider_y,
                                  int width, int height,
                                  int scale,
                                  const TypioCandidatePopupPalette *palette,
                                  TypioCandidatePopupBuffer **out_buffer) {
    int buffer_width;
    int buffer_height;
    TypioCandidatePopupBuffer *buffer;
    cairo_surface_t *surface;
    cairo_t *cr;

    if (!target || !target->surface || !target->shm || !target->buffers ||
        !palette) {
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

    if (preedit_layout) {
        popup_draw_layout(cr, preedit_layout, preedit_x, preedit_y,
                        palette->preedit_r, palette->preedit_g, palette->preedit_b);
    }

    for (size_t i = 0; i < line_count; ++i) {
        const TypioCandidatePopupLine *line = &lines[i];
        bool is_selected = (selected >= 0 && (size_t)selected == i);
        popup_repaint_row(cr, line, is_selected, palette);
    }

    if (mode_label_layout) {
        if (mode_label_divider_y >= 0) {
            cairo_set_source_rgba(cr, palette->border_r, palette->border_g,
                                  palette->border_b, palette->border_a * 0.5);
            cairo_move_to(cr, TYPIO_CANDIDATE_POPUP_PADDING, mode_label_divider_y + 0.5);
            cairo_line_to(cr, width - TYPIO_CANDIDATE_POPUP_PADDING, mode_label_divider_y + 0.5);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }
        popup_draw_layout(cr, mode_label_layout, mode_label_x, mode_label_y,
                          palette->muted_r, palette->muted_g, palette->muted_b);
    }

    cairo_surface_flush(surface);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    wl_surface_set_buffer_scale(target->surface, scale);
    wl_surface_attach(target->surface, buffer->buffer, 0, 0);
    wl_surface_damage(target->surface, 0, 0, width, height);
    wl_surface_commit(target->surface);
    buffer->busy = true;

    if (out_buffer) {
        *out_buffer = buffer;
    }
    return true;
}

bool typio_candidate_popup_paint_selection_update(
                                  const TypioCandidatePopupPaintTarget *target,
                                  const TypioCandidatePopupLine *lines,
                                  size_t line_count,
                                  int old_selected,
                                  int new_selected,
                                  int width, int height,
                                  int scale,
                                  const TypioCandidatePopupPalette *palette,
                                  const TypioCandidatePopupBuffer *source_buffer,
                                  TypioCandidatePopupBuffer **out_buffer) {
    int buffer_width;
    int buffer_height;
    TypioCandidatePopupBuffer *buffer;
    cairo_surface_t *surface;
    cairo_t *cr;

    if (!target || !target->surface || !target->shm || !target->buffers ||
        !palette || !source_buffer || !source_buffer->data) {
        return false;
    }

    if (!popup_scaled_dimension(width, scale, &buffer_width) ||
        !popup_scaled_dimension(height, scale, &buffer_height)) {
        return false;
    }

    if (source_buffer->width != buffer_width || source_buffer->height != buffer_height) {
        return false;
    }

    buffer = typio_candidate_popup_buffer_acquire(target->buffers, target->buffer_count,
                                        target->shm, buffer_width, buffer_height);
    if (!buffer) {
        return false;
    }

    if (buffer != source_buffer) {
        memcpy(buffer->data, source_buffer->data, source_buffer->size);
    }

    surface = cairo_image_surface_create_for_data((unsigned char *)buffer->data,
                                                  CAIRO_FORMAT_ARGB32,
                                                  buffer_width, buffer_height,
                                                  buffer->stride);
    cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);

    if (old_selected >= 0 && (size_t)old_selected < line_count) {
        popup_repaint_row(cr, &lines[old_selected], false, palette);
    }

    if (new_selected >= 0 && (size_t)new_selected < line_count) {
        popup_repaint_row(cr, &lines[new_selected], true, palette);
    }

    cairo_surface_flush(surface);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    wl_surface_set_buffer_scale(target->surface, scale);
    wl_surface_attach(target->surface, buffer->buffer, 0, 0);

    if (old_selected >= 0 && (size_t)old_selected < line_count) {
        wl_surface_damage(target->surface, lines[old_selected].x, lines[old_selected].y,
                          lines[old_selected].width, lines[old_selected].height);
    }
    if (new_selected >= 0 && (size_t)new_selected < line_count) {
        wl_surface_damage(target->surface, lines[new_selected].x, lines[new_selected].y,
                          lines[new_selected].width, lines[new_selected].height);
    }

    wl_surface_commit(target->surface);
    buffer->busy = true;

    if (out_buffer) {
        *out_buffer = buffer;
    }
    return true;
}

bool typio_candidate_popup_paint_aux_update(
                                  const TypioCandidatePopupPaintTarget *target,
                                  int width, int height,
                                  int scale,
                                  const TypioCandidatePopupPalette *palette,
                                  const TypioCandidatePopupBuffer *source_buffer,
                                  PangoLayout *old_preedit_layout,
                                  int old_preedit_x,
                                  int old_preedit_y,
                                  int old_preedit_width,
                                  int old_preedit_height,
                                  PangoLayout *new_preedit_layout,
                                  int new_preedit_x,
                                  int new_preedit_y,
                                  int new_preedit_width,
                                  int new_preedit_height,
                                  PangoLayout *old_mode_label_layout,
                                  int old_mode_label_x,
                                  int old_mode_label_y,
                                  int old_mode_label_width,
                                  int old_mode_label_height,
                                  int old_mode_label_divider_y,
                                  PangoLayout *new_mode_label_layout,
                                  int new_mode_label_x,
                                  int new_mode_label_y,
                                  int new_mode_label_width,
                                  int new_mode_label_height,
                                  int new_mode_label_divider_y,
                                  TypioCandidatePopupBuffer **out_buffer) {
    int buffer_width;
    int buffer_height;
    TypioCandidatePopupBuffer *buffer;
    cairo_surface_t *surface;
    cairo_t *cr;
    TypioCandidatePopupDamageLine damage_lines[6] = {};
    size_t damage_count = 0;
    TypioCandidatePopupDamageRect damage = {};

    if (!target || !target->surface || !target->shm || !target->buffers ||
        !palette || !source_buffer || !source_buffer->data) {
        return false;
    }

    if (!popup_scaled_dimension(width, scale, &buffer_width) ||
        !popup_scaled_dimension(height, scale, &buffer_height)) {
        return false;
    }

    if (source_buffer->width != buffer_width || source_buffer->height != buffer_height) {
        return false;
    }

    buffer = typio_candidate_popup_buffer_acquire(target->buffers, target->buffer_count,
                                        target->shm, buffer_width, buffer_height);
    if (!buffer) {
        return false;
    }

    if (buffer != source_buffer) {
        memcpy(buffer->data, source_buffer->data, source_buffer->size);
    }

    popup_append_damage_line(damage_lines, &damage_count,
                             old_preedit_x, old_preedit_y,
                             old_preedit_width, old_preedit_height);
    popup_append_damage_line(damage_lines, &damage_count,
                             new_preedit_x, new_preedit_y,
                             new_preedit_width, new_preedit_height);
    popup_append_damage_line(damage_lines, &damage_count,
                             old_mode_label_x, old_mode_label_y,
                             old_mode_label_width, old_mode_label_height);
    popup_append_damage_line(damage_lines, &damage_count,
                             new_mode_label_x, new_mode_label_y,
                             new_mode_label_width, new_mode_label_height);
    if (old_mode_label_layout && old_mode_label_divider_y >= 0) {
        popup_append_damage_line(damage_lines, &damage_count,
                                 TYPIO_CANDIDATE_POPUP_PADDING, old_mode_label_divider_y,
                                 width - TYPIO_CANDIDATE_POPUP_PADDING * 2, 1);
    }
    if (new_mode_label_layout && new_mode_label_divider_y >= 0) {
        popup_append_damage_line(damage_lines, &damage_count,
                                 TYPIO_CANDIDATE_POPUP_PADDING, new_mode_label_divider_y,
                                 width - TYPIO_CANDIDATE_POPUP_PADDING * 2, 1);
    }

    if (!typio_candidate_popup_damage_union(damage_lines, damage_count, &damage)) {
        if (out_buffer) {
            *out_buffer = buffer;
        }
        return true;
    }

    surface = cairo_image_surface_create_for_data((unsigned char *)buffer->data,
                                                  CAIRO_FORMAT_ARGB32,
                                                  buffer_width, buffer_height,
                                                  buffer->stride);
    cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);

    for (size_t i = 0; i < damage_count; ++i) {
        popup_clear_rect(cr, &damage_lines[i], palette);
    }

    if (new_preedit_layout) {
        popup_draw_layout(cr, new_preedit_layout, new_preedit_x, new_preedit_y,
                          palette->preedit_r, palette->preedit_g, palette->preedit_b);
    }

    if (new_mode_label_layout) {
        if (new_mode_label_divider_y >= 0) {
            cairo_set_source_rgba(cr, palette->border_r, palette->border_g,
                                  palette->border_b, palette->border_a * 0.5);
            cairo_move_to(cr, TYPIO_CANDIDATE_POPUP_PADDING, new_mode_label_divider_y + 0.5);
            cairo_line_to(cr, width - TYPIO_CANDIDATE_POPUP_PADDING, new_mode_label_divider_y + 0.5);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }
        popup_draw_layout(cr, new_mode_label_layout, new_mode_label_x, new_mode_label_y,
                          palette->muted_r, palette->muted_g, palette->muted_b);
    }

    cairo_surface_flush(surface);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    wl_surface_set_buffer_scale(target->surface, scale);
    wl_surface_attach(target->surface, buffer->buffer, 0, 0);
    wl_surface_damage(target->surface, damage.x, damage.y, damage.width, damage.height);
    wl_surface_commit(target->surface);
    buffer->busy = true;

    if (out_buffer) {
        *out_buffer = buffer;
    }
    return true;
}
