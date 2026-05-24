/**
 * @file candidate_panel_paint.cc
 * @brief Record the candidate panel into a flux canvas (GPU).
 *
 * Rectangles are solid premultiplied fills; glyphs are filled outlines via
 * typio_flux_fill_layout. All coordinates are converted from logical layout
 * pixels to physical surface pixels with the output scale factor.
 */

#include "candidate_panel_paint.h"
#include "flux_renderer.h"

#include <stdint.h>

/* ── Colour helpers ─────────────────────────────────────────────────── */

static inline uint8_t u8(double v)
{
    if (v <= 0.0) return 0;
    if (v >= 1.0) return 255;
    return (uint8_t)(v * 255.0 + 0.5);
}

/* flux canvas colours are premultiplied; flux_color_rgba_premul takes
 * straight 8-bit components and premultiplies by alpha for us. */
static inline flux_color pcol(double r, double g, double b, double a)
{
    return flux_color_rgba_premul(u8(r), u8(g), u8(b), u8(a));
}

static inline flux_rect rect_px(float x, float y, float w, float h, float s)
{
    return (flux_rect){ x * s, y * s, w * s, h * s };
}

/* ── Row + chrome ───────────────────────────────────────────────────── */

static void record_row(flux_canvas *cv, flux_arena *ar,
                       const PopupRow *row, bool selected,
                       const TypioCandidatePanelPalette *p, float s)
{
    if (selected) {
        flux_canvas_fill_rect_color(
            cv, rect_px((float)row->x + 1, (float)row->y + 1,
                        (float)row->w - 2, (float)row->h - 2, s),
            pcol(p->selection_r, p->selection_g, p->selection_b, p->selection_a));
        typio_flux_fill_layout(cv, ar, row->label_layout_sel, row->label_x * s, row->label_y * s);
        typio_flux_fill_layout(cv, ar, row->layout_sel,       row->text_x  * s, row->text_y  * s);
        return;
    }

    /* Background is the canvas clear; only text needs drawing. */
    typio_flux_fill_layout(cv, ar, row->label_layout, row->label_x * s, row->label_y * s);
    typio_flux_fill_layout(cv, ar, row->layout,       row->text_x  * s, row->text_y  * s);
}

static void record_border(flux_canvas *cv, const PopupGeometry *g, float s)
{
    const TypioCandidatePanelPalette *p = g->palette;
    flux_color bc = pcol(p->border_r, p->border_g, p->border_b, p->border_a);
    float W = (float)g->popup_w;
    float H = (float)g->popup_h;

    flux_canvas_fill_rect_color(cv, rect_px(0,     0,     W, 1, s), bc);  /* top    */
    flux_canvas_fill_rect_color(cv, rect_px(0,     H - 1, W, 1, s), bc);  /* bottom */
    flux_canvas_fill_rect_color(cv, rect_px(0,     0,     1, H, s), bc);  /* left   */
    flux_canvas_fill_rect_color(cv, rect_px(W - 1, 0,     1, H, s), bc);  /* right  */
}

static void record_mode_label(flux_canvas *cv, flux_arena *ar,
                              const PopupGeometry *g, float s)
{
    const TypioCandidatePanelPalette *p = g->palette;
    if (!g->mode_layout || g->mode_h <= 0) return;

    if (g->mode_divider_y >= 0) {
        flux_canvas_fill_rect_color(
            cv, rect_px((float)POPUP_PAD_X, (float)g->mode_divider_y + 0.5f,
                        (float)(g->popup_w - 2 * POPUP_PAD_X), 1, s),
            pcol(p->border_r, p->border_g, p->border_b, p->border_a * 0.5));
    }
    typio_flux_fill_layout(cv, ar, g->mode_layout, g->mode_x * s, g->mode_y * s);
}

/* ── Public API ─────────────────────────────────────────────────────── */

void popup_record(const PopupPaintTarget *target,
                  const PopupGeometry *geom,
                  int selected)
{
    if (!target || !target->canvas || !geom || !geom->palette) return;

    flux_canvas *cv = target->canvas;
    flux_arena  *ar = target->arena;
    float        s  = geom->scale > 0.0f ? geom->scale : 1.0f;

    record_border(cv, geom, s);

    if (geom->preedit_layout) {
        typio_flux_fill_layout(cv, ar, geom->preedit_layout,
                               geom->pre_x * s, geom->pre_y * s);
    }

    for (size_t i = 0; i < geom->row_count; ++i) {
        bool sel = selected >= 0 && (size_t)selected == i;
        record_row(cv, ar, &geom->rows[i], sel, geom->palette, s);
    }

    record_mode_label(cv, ar, geom, s);
}
