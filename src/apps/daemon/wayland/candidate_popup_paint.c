/**
 * @file candidate_popup_paint.c
 * @brief Cairo paint paths for the candidate popup.
 *
 * All three render paths share the same low-level drawing helpers.
 * pango_cairo_update_layout() is called before every pango_cairo_show_layout()
 * so that Pango adapts font metrics to the scaled Cairo context — this is the
 * correct handshake for layouts created via PangoContext without Cairo.
 */

#include "candidate_popup_paint.h"

#include <cairo.h>
#include <pango/pangocairo.h>
#include <wayland-client.h>
#include <string.h>

/* ── Scaling helper ─────────────────────────────────────────────────── */

static bool scaled(int logical, int scale, int *physical) {
    if (!physical || logical < 0 || scale < 1) return false;
    *physical = logical * scale;
    return true;
}

/* ── Low-level drawing ──────────────────────────────────────────────── */

static void draw_layout(cairo_t *cr, PangoLayout *layout,
                         double x, double y,
                         double r, double g, double b) {
    if (!layout) return;
    pango_cairo_update_layout(cr, layout);
    cairo_move_to(cr, x, y);
    cairo_set_source_rgb(cr, r, g, b);
    pango_cairo_show_layout(cr, layout);
}

static void draw_row(cairo_t *cr, const PopupRow *row, bool selected,
                      const TypioCandidatePopupPalette *p) {
    /* Background */
    cairo_set_source_rgba(cr, p->bg_r, p->bg_g, p->bg_b, p->bg_a);
    cairo_rectangle(cr, row->x, row->y, row->w, row->h);
    cairo_fill(cr);

    if (selected) {
        cairo_set_source_rgba(cr, p->selection_r, p->selection_g,
                              p->selection_b, p->selection_a);
        cairo_rectangle(cr, row->x, row->y, row->w, row->h);
        cairo_fill(cr);
        draw_layout(cr, row->layout, row->text_x, row->text_y,
                    p->selection_text_r, p->selection_text_g, p->selection_text_b);
    } else {
        draw_layout(cr, row->layout, row->text_x, row->text_y,
                    p->text_r, p->text_g, p->text_b);
    }
}

static void draw_mode_label(cairo_t *cr, const PopupGeometry *g,
                              const TypioCandidatePopupPalette *p) {
    if (!g->mode_layout) return;

    if (g->mode_divider_y >= 0) {
        cairo_set_source_rgba(cr, p->border_r, p->border_g,
                              p->border_b, p->border_a * 0.5);
        cairo_move_to(cr, POPUP_PADDING, g->mode_divider_y + 0.5);
        cairo_line_to(cr, g->popup_w - POPUP_PADDING, g->mode_divider_y + 0.5);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    }

    draw_layout(cr, g->mode_layout,
                g->mode_x, g->mode_y,
                p->muted_r, p->muted_g, p->muted_b);
}

/* ── Buffer helpers ─────────────────────────────────────────────────── */

static TypioCandidatePopupBuffer *acquire_buffer(const PopupPaintTarget *t,
                                                   int bw, int bh) {
    return typio_candidate_popup_buffer_acquire(t->buffers, t->buffer_count,
                                                t->shm, bw, bh);
}

static cairo_t *open_buffer_cr(TypioCandidatePopupBuffer *buf,
                                 cairo_surface_t **out_surface,
                                 int bw, int bh, int scale) {
    cairo_surface_t *surf;
    cairo_t *cr;

    surf = cairo_image_surface_create_for_data(
        (unsigned char *)buf->data, CAIRO_FORMAT_ARGB32,
        bw, bh, buf->stride);
    cr   = cairo_create(surf);
    cairo_scale(cr, scale, scale);
    *out_surface = surf;
    return cr;
}

static void commit_buffer(const PopupPaintTarget *t,
                           TypioCandidatePopupBuffer *buf,
                           int scale,
                           int damage_x, int damage_y,
                           int damage_w, int damage_h) {
    wl_surface_set_buffer_scale(t->surface, scale);
    wl_surface_attach(t->surface, buf->buffer, 0, 0);
    wl_surface_damage(t->surface, damage_x, damage_y, damage_w, damage_h);
    wl_surface_commit(t->surface);
    buf->busy = true;
}

/* ── Public API ─────────────────────────────────────────────────────── */

bool popup_paint_full(const PopupPaintTarget *target,
                      const PopupGeometry *geom,
                      int selected,
                      TypioCandidatePopupBuffer **out_buf) {
    int bw, bh;
    TypioCandidatePopupBuffer *buf;
    cairo_surface_t *surf;
    cairo_t *cr;
    size_t i;
    const TypioCandidatePopupPalette *p = geom->palette;

    if (!target || !target->surface || !target->shm || !geom || !p) return false;

    if (!scaled(geom->popup_w, geom->scale, &bw) ||
        !scaled(geom->popup_h, geom->scale, &bh)) return false;

    buf = acquire_buffer(target, bw, bh);
    if (!buf) return false;

    memset(buf->data, 0, buf->size);
    cr = open_buffer_cr(buf, &surf, bw, bh, geom->scale);

    /* Background */
    cairo_set_source_rgba(cr, p->bg_r, p->bg_g, p->bg_b, p->bg_a);
    cairo_paint(cr);

    /* Border */
    cairo_set_source_rgba(cr, p->border_r, p->border_g, p->border_b, p->border_a);
    cairo_rectangle(cr, 0.5, 0.5, geom->popup_w - 1.0, geom->popup_h - 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* Preedit */
    if (geom->preedit_layout) {
        draw_layout(cr, geom->preedit_layout, geom->pre_x, geom->pre_y,
                    p->preedit_r, p->preedit_g, p->preedit_b);
    }

    /* Candidate rows */
    for (i = 0; i < geom->row_count; ++i) {
        bool is_sel = (selected >= 0 && (size_t)selected == i);
        draw_row(cr, &geom->rows[i], is_sel, p);
    }

    /* Mode label */
    draw_mode_label(cr, geom, p);

    cairo_surface_flush(surf);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    commit_buffer(target, buf, geom->scale, 0, 0, geom->popup_w, geom->popup_h);

    if (out_buf) *out_buf = buf;
    return true;
}

bool popup_paint_selection(const PopupPaintTarget *target,
                            const PopupGeometry *geom,
                            int old_sel,
                            int new_sel,
                            const TypioCandidatePopupBuffer *src,
                            TypioCandidatePopupBuffer **out_buf) {
    int bw, bh;
    TypioCandidatePopupBuffer *buf;
    cairo_surface_t *surf;
    cairo_t *cr;
    const TypioCandidatePopupPalette *p = geom->palette;

    if (!target || !target->surface || !target->shm || !geom ||
        !src || !src->data || !p) return false;

    if (!scaled(geom->popup_w, geom->scale, &bw) ||
        !scaled(geom->popup_h, geom->scale, &bh)) return false;

    if (src->width != bw || src->height != bh) return false;

    buf = acquire_buffer(target, bw, bh);
    if (!buf) return false;

    if (buf != src) {
        memcpy(buf->data, src->data, src->size);
    }

    cr = open_buffer_cr(buf, &surf, bw, bh, geom->scale);

    /* Repaint only the two affected rows */
    if (old_sel >= 0 && (size_t)old_sel < geom->row_count) {
        draw_row(cr, &geom->rows[old_sel], false, p);
    }
    if (new_sel >= 0 && (size_t)new_sel < geom->row_count) {
        draw_row(cr, &geom->rows[new_sel], true, p);
    }

    cairo_surface_flush(surf);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    /* Damage only the repainted rows */
    wl_surface_set_buffer_scale(target->surface, geom->scale);
    wl_surface_attach(target->surface, buf->buffer, 0, 0);
    if (old_sel >= 0 && (size_t)old_sel < geom->row_count) {
        const PopupRow *r = &geom->rows[old_sel];
        wl_surface_damage(target->surface, r->x, r->y, r->w, r->h);
    }
    if (new_sel >= 0 && (size_t)new_sel < geom->row_count) {
        const PopupRow *r = &geom->rows[new_sel];
        wl_surface_damage(target->surface, r->x, r->y, r->w, r->h);
    }
    wl_surface_commit(target->surface);
    buf->busy = true;

    if (out_buf) *out_buf = buf;
    return true;
}

bool popup_paint_aux(const PopupPaintTarget *target,
                      const PopupGeometry *old_geom,
                      const PopupGeometry *new_geom,
                      const TypioCandidatePopupBuffer *src,
                      TypioCandidatePopupBuffer **out_buf) {
    int bw, bh;
    TypioCandidatePopupBuffer *buf;
    cairo_surface_t *surf;
    cairo_t *cr;
    const TypioCandidatePopupPalette *p = new_geom->palette;

    if (!target || !target->surface || !target->shm ||
        !old_geom || !new_geom || !src || !src->data || !p) return false;

    if (new_geom->popup_w != old_geom->popup_w ||
        new_geom->popup_h != old_geom->popup_h) return false;

    if (!scaled(new_geom->popup_w, new_geom->scale, &bw) ||
        !scaled(new_geom->popup_h, new_geom->scale, &bh)) return false;

    if (src->width != bw || src->height != bh) return false;

    buf = acquire_buffer(target, bw, bh);
    if (!buf) return false;

    if (buf != src) {
        memcpy(buf->data, src->data, src->size);
    }

    cr = open_buffer_cr(buf, &surf, bw, bh, new_geom->scale);

    /* Erase old preedit region */
    if (old_geom->preedit_layout && old_geom->pre_w > 0 && old_geom->pre_h > 0) {
        cairo_set_source_rgba(cr, p->bg_r, p->bg_g, p->bg_b, p->bg_a);
        cairo_rectangle(cr, old_geom->pre_x, old_geom->pre_y,
                         old_geom->pre_w, old_geom->pre_h);
        cairo_fill(cr);
    }

    /* Erase old mode label region */
    if (old_geom->mode_layout && old_geom->mode_w > 0 && old_geom->mode_h > 0) {
        cairo_set_source_rgba(cr, p->bg_r, p->bg_g, p->bg_b, p->bg_a);
        cairo_rectangle(cr, old_geom->mode_x, old_geom->mode_y,
                         old_geom->mode_w, old_geom->mode_h);
        cairo_fill(cr);
        /* Erase divider line too if present */
        if (old_geom->mode_divider_y >= 0) {
            cairo_rectangle(cr, POPUP_PADDING, old_geom->mode_divider_y,
                             new_geom->popup_w - POPUP_PADDING * 2, 1);
            cairo_fill(cr);
        }
    }

    /* Draw new preedit */
    if (new_geom->preedit_layout) {
        draw_layout(cr, new_geom->preedit_layout,
                    new_geom->pre_x, new_geom->pre_y,
                    p->preedit_r, p->preedit_g, p->preedit_b);
    }

    /* Draw new mode label */
    draw_mode_label(cr, new_geom, p);

    cairo_surface_flush(surf);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    /* Damage the union of old and new aux regions */
    int damage_x = POPUP_PADDING;
    int damage_y = 0;
    int damage_w = new_geom->popup_w - POPUP_PADDING * 2;
    int damage_h = new_geom->popup_h;
    commit_buffer(target, buf, new_geom->scale,
                  damage_x, damage_y, damage_w, damage_h);

    if (out_buf) *out_buf = buf;
    return true;
}
