/**
 * @file candidate_popup_paint.cc
 * @brief Flux paint paths for the candidate popup.
 */

#include "candidate_popup_paint.h"
#include "flux_renderer.h"

#include <wayland-client.h>

#include <string.h>

/* ── Helpers ────────────────────────────────────────────────────────── */

static fx_color palette_color(double r, double g, double b, double a) {
    TypioColor c = {(float)r, (float)g, (float)b, (float)a};
    return typio_flux_color(c);
}

static bool scaled(int logical, int scale, int *physical) {
    if (!physical || logical < 0 || scale < 1) return false;
    *physical = logical * scale;
    return true;
}

static fx_rect rect_px(float x, float y, float w, float h, int scale) {
    return (fx_rect){
        .x = x * (float)scale,
        .y = y * (float)scale,
        .w = w * (float)scale,
        .h = h * (float)scale,
    };
}

static bool draw_layout(fx_canvas *canvas,
                        TypioTextLayout *layout,
                        float x,
                        float y,
                        int scale) {
    if (!layout) return true;
    return typio_flux_draw_layout(canvas, layout,
                                  x * (float)scale,
                                  y * (float)scale);
}

static void draw_row(fx_canvas *canvas,
                     const PopupRow *row,
                     bool selected,
                     const TypioCandidatePopupPalette *p,
                     int scale) {
    if (!canvas || !row || !p) return;

    if (selected) {
        fx_rect highlight = rect_px((float)row->x + 1.0f,
                                    (float)row->y + 1.0f,
                                    (float)row->w - 2.0f,
                                    (float)row->h - 2.0f,
                                    scale);
        fx_fill_rect(canvas, &highlight,
                     palette_color(p->selection_r, p->selection_g,
                                   p->selection_b, p->selection_a));
        draw_layout(canvas, row->label_layout_sel, row->label_x, row->label_y, scale);
        draw_layout(canvas, row->layout_sel, row->text_x, row->text_y, scale);
        return;
    }

    fx_rect bg = rect_px((float)row->x, (float)row->y,
                         (float)row->w, (float)row->h, scale);
    fx_fill_rect(canvas, &bg, palette_color(p->bg_r, p->bg_g, p->bg_b, p->bg_a));
    draw_layout(canvas, row->label_layout, row->label_x, row->label_y, scale);
    draw_layout(canvas, row->layout, row->text_x, row->text_y, scale);
}

static void draw_mode_label(fx_canvas *canvas,
                            const PopupGeometry *g,
                            const TypioCandidatePopupPalette *p) {
    if (!canvas || !g || !p || !g->mode_layout) return;

    if (g->mode_divider_y >= 0) {
        fx_rect divider = rect_px((float)POPUP_PAD_X,
                                  (float)g->mode_divider_y + 0.5f,
                                  (float)(g->popup_w - 2 * POPUP_PAD_X),
                                  1.0f,
                                  g->scale);
        fx_fill_rect(canvas, &divider,
                     palette_color(p->border_r, p->border_g, p->border_b,
                                   p->border_a * 0.5));
    }

    draw_layout(canvas, g->mode_layout, g->mode_x, g->mode_y, g->scale);
}

static void draw_border(fx_canvas *canvas,
                        int width,
                        int height,
                        const TypioCandidatePopupPalette *p) {
    fx_color color = palette_color(p->border_r, p->border_g, p->border_b, p->border_a);
    fx_rect top = {0.0f, 0.0f, (float)width, 1.0f};
    fx_rect bottom = {0.0f, (float)height - 1.0f, (float)width, 1.0f};
    fx_rect left = {0.0f, 0.0f, 1.0f, (float)height};
    fx_rect right = {(float)width - 1.0f, 0.0f, 1.0f, (float)height};
    fx_fill_rect(canvas, &top, color);
    fx_fill_rect(canvas, &bottom, color);
    fx_fill_rect(canvas, &left, color);
    fx_fill_rect(canvas, &right, color);
}

/* ── Core render (reuses persistent offscreen surface) ──────────────── */

static bool render_to_buffer(PopupRenderCtx *pc,
                             TypioCandidatePopupBuffer *buf,
                             int bw,
                             int bh,
                             const PopupGeometry *geom,
                             int selected) {
    fx_context *ctx = typio_flux_context_get();
    if (!ctx || !buf || !buf->data || !geom || !geom->palette) return false;

    /* Reuse persistent offscreen surface; recreate only on resize. */
    if (!pc->flux_surface || pc->flux_surface_w != bw || pc->flux_surface_h != bh) {
        if (pc->flux_surface) {
            fx_surface_destroy(pc->flux_surface);
            pc->flux_surface = NULL;
        }
        pc->flux_surface = fx_surface_create_offscreen(ctx, bw, bh,
                                                        FX_FMT_BGRA8_UNORM,
                                                        FX_CS_SRGB);
        pc->flux_surface_w = bw;
        pc->flux_surface_h = bh;
    }
    if (!pc->flux_surface) return false;

    fx_surface *surface = pc->flux_surface;
    fx_canvas *canvas = fx_surface_acquire(surface);
    if (!canvas) return false;

    const TypioCandidatePopupPalette *p = geom->palette;

    fx_clear(canvas, 0x00000000U);
    fx_rect bg = {0.0f, 0.0f, (float)bw, (float)bh};
    fx_fill_rect(canvas, &bg, palette_color(p->bg_r, p->bg_g, p->bg_b, p->bg_a));
    draw_border(canvas, bw, bh, p);

    if (geom->preedit_layout) {
        draw_layout(canvas, geom->preedit_layout, geom->pre_x, geom->pre_y, geom->scale);
    }
    for (size_t i = 0; i < geom->row_count; ++i) {
        draw_row(canvas, &geom->rows[i],
                 selected >= 0 && (size_t)selected == i, p, geom->scale);
    }
    draw_mode_label(canvas, geom, p);

    fx_surface_present(surface);
    bool ok = fx_surface_read_pixels(surface, buf->data, buf->stride);
    return ok;
}

static void commit_buffer(const PopupPaintTarget *target,
                          TypioCandidatePopupBuffer *buf,
                          int scale,
                          int dx,
                          int dy,
                          int dw,
                          int dh) {
    wl_surface_set_buffer_scale(target->surface, scale);
    wl_surface_attach(target->surface, buf->buffer, 0, 0);
    wl_surface_damage(target->surface, dx, dy, dw, dh);
    wl_surface_commit(target->surface);
    buf->busy = true;
}

/* ── Public API ─────────────────────────────────────────────────────── */

bool popup_paint_full(PopupRenderCtx *pc,
                      const PopupPaintTarget *target,
                      const PopupGeometry *geom,
                      int selected,
                      TypioCandidatePopupBuffer **out_buf) {
    int bw, bh;
    if (!target || !geom || !geom->palette) return false;
    if (!scaled(geom->popup_w, geom->scale, &bw) || !scaled(geom->popup_h, geom->scale, &bh)) return false;

    TypioCandidatePopupBuffer *buf = typio_candidate_popup_buffer_acquire(
        target->buffers, target->buffer_count, target->shm, bw, bh);
    if (!buf) return false;

    memset(buf->data, 0, buf->size);
    if (!render_to_buffer(pc, buf, bw, bh, geom, selected)) {
        return false;
    }

    commit_buffer(target, buf, geom->scale, 0, 0, geom->popup_w, geom->popup_h);
    if (out_buf) *out_buf = buf;
    return true;
}

bool popup_paint_selection(PopupRenderCtx *pc,
                           const PopupPaintTarget *target,
                           const PopupGeometry *geom,
                           int old_sel,
                           int new_sel,
                           const TypioCandidatePopupBuffer *src,
                           TypioCandidatePopupBuffer **out_buf) {
    (void)old_sel;
    (void)src;
    return popup_paint_full(pc, target, geom, new_sel, out_buf);
}

bool popup_paint_aux(PopupRenderCtx *pc,
                     const PopupPaintTarget *target,
                     const PopupGeometry *old_geom,
                     const PopupGeometry *new_geom,
                     int selected,
                     const TypioCandidatePopupBuffer *src,
                     TypioCandidatePopupBuffer **out_buf) {
    (void)old_geom;
    (void)src;
    return popup_paint_full(pc, target, new_geom, selected, out_buf);
}
