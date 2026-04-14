/**
 * @file candidate_popup_paint.cc
 * @brief Skia paint paths for the candidate popup.
 */

#include "candidate_popup_paint.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRRect.h"
#include "modules/skparagraph/include/Paragraph.h"
#include <wayland-client.h>
#include <string.h>

/* ── Helpers ────────────────────────────────────────────────────────── */

static inline SkColor palette_color(double r, double g, double b, double a) {
    return SkColorSetARGB((uint8_t)(a * 255.0),
                          (uint8_t)(r * 255.0),
                          (uint8_t)(g * 255.0),
                          (uint8_t)(b * 255.0));
}

static bool scaled(int logical, int scale, int *physical) {
    if (!physical || logical < 0 || scale < 1) return false;
    *physical = logical * scale;
    return true;
}

/* Draw a paragraph with an explicit ink color, overriding the built-in TextStyle color.
 * We use saveLayer + drawPaint(kSrcIn) so the alpha mask from the paragraph is preserved
 * while replacing its RGB with the requested color. */
static void draw_layout(SkCanvas *canvas, TypioTextLayout *layout, float x, float y,
                         SkColor color) {
    if (!layout) return;
    using namespace skia::textlayout;
    Paragraph *para = (Paragraph *)layout;
    canvas->saveLayer(nullptr, nullptr);
    para->paint(canvas, x, y);
    SkPaint recolor;
    recolor.setColor(color);
    recolor.setBlendMode(SkBlendMode::kSrcIn);
    canvas->drawPaint(recolor);
    canvas->restore();
}

static void draw_row(SkCanvas *canvas, const PopupRow *row, bool selected,
                      const TypioCandidatePopupPalette *p) {
    SkPaint paint;
    paint.setAntiAlias(true);

    if (selected) {
        /* Rounded selection highlight — 6 px radius, inset 1 px from row edge */
        paint.setColor(palette_color(p->selection_r, p->selection_g,
                                     p->selection_b, p->selection_a));
        SkRRect rrect;
        rrect.setRectXY(SkRect::MakeXYWH((float)row->x + 1.0f, (float)row->y + 1.0f,
                                          (float)row->w - 2.0f, (float)row->h - 2.0f),
                         6.0f, 6.0f);
        canvas->drawRRect(rrect, paint);

        SkColor sel = palette_color(p->selection_text_r, p->selection_text_g,
                                     p->selection_text_b, 1.0);
        draw_layout(canvas, row->label_layout, (float)row->label_x, (float)row->label_y, sel);
        draw_layout(canvas, row->layout,       (float)row->text_x,  (float)row->text_y,  sel);
    } else {
        /* Clear background for this row in case we're redrawing over an old selection */
        paint.setColor(palette_color(p->bg_r, p->bg_g, p->bg_b, p->bg_a));
        canvas->drawRect(SkRect::MakeXYWH((float)row->x, (float)row->y, (float)row->w, (float)row->h), paint);

        SkColor label_color = palette_color(p->muted_r, p->muted_g, p->muted_b, 1.0);
        SkColor text_color  = palette_color(p->text_r,  p->text_g,  p->text_b,  1.0);
        draw_layout(canvas, row->label_layout, (float)row->label_x, (float)row->label_y, label_color);
        draw_layout(canvas, row->layout,       (float)row->text_x,  (float)row->text_y,  text_color);
    }
}

static void draw_mode_label(SkCanvas *canvas, const PopupGeometry *g,
                          const TypioCandidatePopupPalette *p) {
    if (!g->mode_layout) return;

    if (g->mode_divider_y >= 0) {
        SkPaint paint;
        paint.setColor(palette_color(p->border_r, p->border_g, p->border_b,
                                     p->border_a * 0.5));
        canvas->drawRect(SkRect::MakeXYWH((float)POPUP_PAD_X,
                                          (float)g->mode_divider_y + 0.5f,
                                          (float)(g->popup_w - 2 * POPUP_PAD_X), 1.0f), paint);
    }

    draw_layout(canvas, g->mode_layout, (float)g->mode_x, (float)g->mode_y,
                palette_color(p->muted_r, p->muted_g, p->muted_b, 1.0));
}

static sk_sp<SkSurface> wrap_buffer(TypioCandidatePopupBuffer *buf, int bw, int bh) {
    /* Wayland SHM uses WL_SHM_FORMAT_ARGB8888 which on little-endian stores
     * bytes as B, G, R, A — matching Skia's kBGRA_8888_SkColorType. */
    SkImageInfo info = SkImageInfo::Make(bw, bh, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
    return SkSurfaces::WrapPixels(info, buf->data, (size_t)buf->stride);
}

static void commit_buffer(const PopupPaintTarget *target,
                           TypioCandidatePopupBuffer *buf,
                           int scale,
                           int dx, int dy, int dw, int dh) {
    wl_surface_set_buffer_scale(target->surface, scale);
    wl_surface_attach(target->surface, buf->buffer, 0, 0);
    wl_surface_damage(target->surface, dx, dy, dw, dh);
    wl_surface_commit(target->surface);
    buf->busy = true;
}

/* ── Public API ─────────────────────────────────────────────────────── */

bool popup_paint_full(const PopupPaintTarget *target,
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
    sk_sp<SkSurface> surface = wrap_buffer(buf, bw, bh);
    SkCanvas *canvas = surface->getCanvas();
    canvas->scale((float)geom->scale, (float)geom->scale);

    const TypioCandidatePopupPalette *p = geom->palette;

    /* Background */
    canvas->clear(SK_ColorTRANSPARENT);

    SkPaint bg_paint;
    bg_paint.setAntiAlias(true);
    bg_paint.setColor(palette_color(p->bg_r, p->bg_g, p->bg_b, p->bg_a));
    SkRRect bg_rrect;
    bg_rrect.setRectXY(SkRect::MakeXYWH(0.0f, 0.0f,
                                        (float)geom->popup_w,
                                        (float)geom->popup_h),
                       12.0f, 12.0f);
    canvas->drawRRect(bg_rrect, bg_paint);

    /* Border */
    SkPaint border_paint;
    border_paint.setAntiAlias(true);
    border_paint.setStyle(SkPaint::kStroke_Style);
    border_paint.setStrokeWidth(1.0f);
    border_paint.setColor(palette_color(p->border_r, p->border_g, p->border_b, p->border_a));
    SkRRect border_rrect;
    border_rrect.setRectXY(SkRect::MakeXYWH(0.5f, 0.5f,
                                            (float)geom->popup_w - 1.0f,
                                            (float)geom->popup_h - 1.0f),
                           11.5f, 11.5f);
    canvas->drawRRect(border_rrect, border_paint);

    /* Preedit */
    if (geom->preedit_layout) {
        draw_layout(canvas, geom->preedit_layout, (float)geom->pre_x, (float)geom->pre_y,
                    palette_color(p->preedit_r, p->preedit_g, p->preedit_b, 1.0));
    }

    /* Rows */
    for (size_t i = 0; i < geom->row_count; ++i) {
        draw_row(canvas, &geom->rows[i], (selected >= 0 && (size_t)selected == i), p);
    }

    /* Mode */
    draw_mode_label(canvas, geom, p);

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
    if (!target || !geom || !src || !src->data) return false;
    if (!scaled(geom->popup_w, geom->scale, &bw) || !scaled(geom->popup_h, geom->scale, &bh)) return false;
    if (src->width != bw || src->height != bh) return false;

    TypioCandidatePopupBuffer *buf = typio_candidate_popup_buffer_acquire(
        target->buffers, target->buffer_count, target->shm, bw, bh);
    if (!buf) return false;

    if (buf != src) memcpy(buf->data, src->data, src->size);

    sk_sp<SkSurface> surface = wrap_buffer(buf, bw, bh);
    SkCanvas *canvas = surface->getCanvas();
    canvas->scale((float)geom->scale, (float)geom->scale);

    const TypioCandidatePopupPalette *p = geom->palette;
    if (old_sel >= 0 && (size_t)old_sel < geom->row_count)
        draw_row(canvas, &geom->rows[old_sel], false, p);
    if (new_sel >= 0 && (size_t)new_sel < geom->row_count)
        draw_row(canvas, &geom->rows[new_sel], true, p);

    if (old_sel >= 0 && (size_t)old_sel < geom->row_count) {
        const PopupRow *r = &geom->rows[old_sel];
        commit_buffer(target, buf, geom->scale, r->x, r->y, r->w, r->h);
    }
    if (new_sel >= 0 && (size_t)new_sel < geom->row_count) {
        const PopupRow *r = &geom->rows[new_sel];
        commit_buffer(target, buf, geom->scale, r->x, r->y, r->w, r->h);
    }

    if (out_buf) *out_buf = buf;
    return true;
}

bool popup_paint_aux(const PopupPaintTarget *target,
                     const PopupGeometry *old_geom,
                     const PopupGeometry *new_geom,
                     const TypioCandidatePopupBuffer *src,
                     TypioCandidatePopupBuffer **out_buf) {
    int bw, bh;
    if (!target || !old_geom || !new_geom || !src || !src->data) return false;
    if (!scaled(new_geom->popup_w, new_geom->scale, &bw) || !scaled(new_geom->popup_h, new_geom->scale, &bh)) return false;
    if (src->width != bw || src->height != bh) return false;

    TypioCandidatePopupBuffer *buf = typio_candidate_popup_buffer_acquire(
        target->buffers, target->buffer_count, target->shm, bw, bh);
    if (!buf) return false;

    if (buf != src) memcpy(buf->data, src->data, src->size);

    sk_sp<SkSurface> surface = wrap_buffer(buf, bw, bh);
    SkCanvas *canvas = surface->getCanvas();
    canvas->scale((float)new_geom->scale, (float)new_geom->scale);

    const TypioCandidatePopupPalette *p = new_geom->palette;

    SkPaint paint;
    paint.setColor(palette_color(p->bg_r, p->bg_g, p->bg_b, p->bg_a));

    if (old_geom->preedit_layout && old_geom->pre_w > 0) {
        canvas->drawRect(SkRect::MakeXYWH((float)old_geom->pre_x, (float)old_geom->pre_y,
                                          (float)old_geom->pre_w, (float)old_geom->pre_h), paint);
    }
    if (old_geom->mode_layout && old_geom->mode_w > 0) {
        canvas->drawRect(SkRect::MakeXYWH((float)old_geom->mode_x, (float)old_geom->mode_y,
                                          (float)old_geom->mode_w, (float)old_geom->mode_h), paint);
        if (old_geom->mode_divider_y >= 0) {
            canvas->drawRect(SkRect::MakeXYWH((float)POPUP_PAD_X, (float)old_geom->mode_divider_y,
                                              (float)(new_geom->popup_w - POPUP_PAD_X * 2), 1.0f), paint);
        }
    }

    if (new_geom->preedit_layout)
        draw_layout(canvas, new_geom->preedit_layout,
                    (float)new_geom->pre_x, (float)new_geom->pre_y,
                    palette_color(p->preedit_r, p->preedit_g, p->preedit_b, 1.0));
    draw_mode_label(canvas, new_geom, p);

    commit_buffer(target, buf, new_geom->scale, POPUP_PAD_X, 0,
                  new_geom->popup_w - POPUP_PAD_X * 2, new_geom->popup_h);
    if (out_buf) *out_buf = buf;
    return true;
}
