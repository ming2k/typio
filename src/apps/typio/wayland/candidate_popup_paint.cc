/**
 * @file candidate_popup_paint.cc
 * @brief CPU paint paths for the candidate popup.
 *
 * Renders into a Wayland SHM buffer (WL_SHM_FORMAT_ARGB8888) using
 * direct CPU blitting.  Text glyphs are rasterized via FreeType inside
 * typio_flux_draw_layout; rectangles are filled inline here.
 */

#include "candidate_popup_paint.h"
#include "flux_renderer.h"

#include <wayland-client.h>

#include <stdint.h>
#include <string.h>

/* ── CPU helpers ────────────────────────────────────────────────────── */

/* Pixel layout: WL_SHM_FORMAT_ARGB8888, little-endian.
 * As uint32_t: bits 31..24 = A, 23..16 = R, 15..8 = G, 7..0 = B.  */

static inline uint32_t pack_argb(double r, double g, double b, double a)
{
    auto u8 = [](double v) -> uint32_t {
        if (v <= 0.0) return 0;
        if (v >= 1.0) return 255;
        return (uint32_t)(v * 255.0 + 0.5);
    };
    return (0xffu << 24) | (u8(r) << 16) | (u8(g) << 8) | u8(b);
    (void)a; /* alpha compositing not needed for opaque rects */
}

static void fill_rect(void *buf, int stride, int buf_h,
                      int x, int y, int w, int h,
                      double r, double g, double b, double a)
{
    if (w <= 0 || h <= 0) return;
    uint32_t color = pack_argb(r, g, b, a);
    for (int row = y; row < y + h; ++row) {
        if (row < 0 || row >= buf_h) continue;
        uint32_t *line = (uint32_t *)((uint8_t *)buf + row * stride);
        for (int col = x; col < x + w; ++col) {
            if (col < 0 || col * 4 + 3 >= stride) continue;
            line[col] = color;
        }
    }
}

static bool scaled(int logical, int scale, int *physical)
{
    if (!physical || logical < 0 || scale < 1) return false;
    *physical = logical * scale;
    return true;
}

/* ── Draw helpers ───────────────────────────────────────────────────── */

static bool draw_layout(void *buf, int stride, int buf_h,
                        TypioTextLayout *layout,
                        float x, float y, int scale)
{
    if (!layout) return true;
    return typio_flux_draw_layout(buf, stride, buf_h, layout,
                                  x * (float)scale,
                                  y * (float)scale);
}

static void draw_row(void *buf, int stride, int buf_h,
                     const PopupRow *row,
                     bool selected,
                     const TypioCandidatePopupPalette *p,
                     int scale)
{
    if (!buf || !row || !p) return;

    if (selected) {
        fill_rect(buf, stride, buf_h,
                  (int)((float)(row->x + 1) * (float)scale),
                  (int)((float)(row->y + 1) * (float)scale),
                  (int)((float)(row->w - 2) * (float)scale),
                  (int)((float)(row->h - 2) * (float)scale),
                  p->selection_r, p->selection_g,
                  p->selection_b, p->selection_a);
        draw_layout(buf, stride, buf_h, row->label_layout_sel,
                    row->label_x, row->label_y, scale);
        draw_layout(buf, stride, buf_h, row->layout_sel,
                    row->text_x, row->text_y, scale);
        return;
    }

    fill_rect(buf, stride, buf_h,
              (int)((float)row->x * (float)scale),
              (int)((float)row->y * (float)scale),
              (int)((float)row->w * (float)scale),
              (int)((float)row->h * (float)scale),
              p->bg_r, p->bg_g, p->bg_b, p->bg_a);
    draw_layout(buf, stride, buf_h, row->label_layout,
                row->label_x, row->label_y, scale);
    draw_layout(buf, stride, buf_h, row->layout,
                row->text_x, row->text_y, scale);
}

static void draw_mode_label(void *buf, int stride, int buf_h,
                            const PopupGeometry *g,
                            const TypioCandidatePopupPalette *p)
{
    if (!buf || !g || !p || !g->mode_layout) return;

    if (g->mode_divider_y >= 0) {
        fill_rect(buf, stride, buf_h,
                  (int)((float)POPUP_PAD_X * (float)g->scale),
                  (int)(((float)g->mode_divider_y + 0.5f) * (float)g->scale),
                  (int)((float)(g->popup_w - 2 * POPUP_PAD_X) * (float)g->scale),
                  g->scale,
                  p->border_r, p->border_g, p->border_b, p->border_a * 0.5);
    }

    draw_layout(buf, stride, buf_h, g->mode_layout,
                g->mode_x, g->mode_y, g->scale);
}

static void draw_border(void *buf, int stride, int buf_h,
                        int width, int height,
                        const TypioCandidatePopupPalette *p)
{
    double r = p->border_r, g = p->border_g, b = p->border_b, a = p->border_a;
    fill_rect(buf, stride, buf_h, 0,          0,           width, 1,      r, g, b, a);
    fill_rect(buf, stride, buf_h, 0,          height - 1,  width, 1,      r, g, b, a);
    fill_rect(buf, stride, buf_h, 0,          0,           1,     height, r, g, b, a);
    fill_rect(buf, stride, buf_h, width - 1,  0,           1,     height, r, g, b, a);
}

/* ── Core render ────────────────────────────────────────────────────── */

static bool render_to_buffer(PopupRenderCtx *pc,
                             TypioCandidatePopupBuffer *buf,
                             int bw, int bh,
                             const PopupGeometry *geom,
                             int selected)
{
    (void)pc;
    if (!buf || !buf->data || !geom || !geom->palette) return false;

    void *pixels  = buf->data;
    int   stride  = buf->stride;
    const TypioCandidatePopupPalette *p = geom->palette;

    /* Background */
    fill_rect(pixels, stride, bh, 0, 0, bw, bh,
              p->bg_r, p->bg_g, p->bg_b, p->bg_a);
    draw_border(pixels, stride, bh, bw, bh, p);

    if (geom->preedit_layout) {
        draw_layout(pixels, stride, bh, geom->preedit_layout,
                    geom->pre_x, geom->pre_y, geom->scale);
    }
    for (size_t i = 0; i < geom->row_count; ++i) {
        draw_row(pixels, stride, bh, &geom->rows[i],
                 selected >= 0 && (size_t)selected == i, p, geom->scale);
    }
    draw_mode_label(pixels, stride, bh, geom, p);

    return true;
}

static void commit_buffer(const PopupPaintTarget *target,
                          TypioCandidatePopupBuffer *buf,
                          int scale, int dx, int dy, int dw, int dh)
{
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
                      TypioCandidatePopupBuffer **out_buf)
{
    int bw, bh;
    if (!target || !geom || !geom->palette) return false;
    if (!scaled(geom->popup_w, geom->scale, &bw) ||
        !scaled(geom->popup_h, geom->scale, &bh)) return false;

    TypioCandidatePopupBuffer *buf = typio_candidate_popup_buffer_acquire(
        target->buffers, target->buffer_count, target->shm, bw, bh);
    if (!buf) return false;

    memset(buf->data, 0, buf->size);
    if (!render_to_buffer(pc, buf, bw, bh, geom, selected))
        return false;

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
                           TypioCandidatePopupBuffer **out_buf)
{
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
                     TypioCandidatePopupBuffer **out_buf)
{
    (void)old_geom;
    (void)src;
    return popup_paint_full(pc, target, new_geom, selected, out_buf);
}
