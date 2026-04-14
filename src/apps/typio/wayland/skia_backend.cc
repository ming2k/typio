/*
 * Bridge C calls to C++ Skia
 * This file is built with the project's C++ compiler.
 */

#include "typio/renderer.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkRect.h"
#include "include/core/SkPaint.h"
#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skparagraph/include/FontCollection.h"

extern "C" {
    typedef struct {
        SkCanvas *canvas;
        SkSurface *surface;
    } SkiaCanvasPriv;

    typedef struct {
        std::unique_ptr<skia::textlayout::Paragraph> paragraph;
    } SkiaTextLayoutPriv;

    static void skia_draw_rect(void *canvas, float x, float y, float w, float h, TypioColor color) {
        SkiaCanvasPriv *priv = (SkiaCanvasPriv *)((TypioCanvas *)canvas)->priv;
        SkPaint paint;
        paint.setColor(SkColorSetARGB((uint8_t)(color.a * 255.0f),
                                      (uint8_t)(color.r * 255.0f),
                                      (uint8_t)(color.g * 255.0f),
                                      (uint8_t)(color.b * 255.0f)));
        paint.setStyle(SkPaint::kFill_Style);
        priv->canvas->drawRect(SkRect::MakeXYWH(x, y, w, h), paint);
    }

    static void skia_draw_text(void *canvas, TypioTextLayout *layout, float x, float y, TypioColor color) {
        SkiaCanvasPriv *priv = (SkiaCanvasPriv *)((TypioCanvas *)canvas)->priv;
        SkiaTextLayoutPriv *text_priv = (SkiaTextLayoutPriv *)layout;
        
        // Paint is currently not used by Paragraph for text color (set via TextStyle)
        // Paragraph already contains styling information.
        text_priv->paragraph->paint(priv->canvas, x, y);
    }

    static void skia_clear(void *canvas) {
        SkiaCanvasPriv *priv = (SkiaCanvasPriv *)((TypioCanvas *)canvas)->priv;
        priv->canvas->clear(SK_ColorTRANSPARENT);
    }

    static TypioCanvasVTable skia_vtable = {
        .draw_rect = skia_draw_rect,
        .draw_text = skia_draw_text,
        .clear = skia_clear
    };
}

extern "C" TypioCanvas *skia_canvas_create(SkCanvas *canvas, SkSurface *surface) {
    TypioCanvas *c = (TypioCanvas *)malloc(sizeof(TypioCanvas));
    SkiaCanvasPriv *priv = (SkiaCanvasPriv *)malloc(sizeof(SkiaCanvasPriv));
    priv->canvas = canvas;
    priv->surface = surface;
    c->priv = priv;
    c->vtable = &skia_vtable;
    return c;
}

extern "C" void skia_canvas_destroy(TypioCanvas *canvas) {
    free(canvas->priv);
    free(canvas);
}
