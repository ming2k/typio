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
#include "modules/skparagraph/include/TypefaceFontProvider.h"
#include <stdlib.h>

using namespace skia::textlayout;

typedef struct {
    sk_sp<FontCollection> font_collection;
} SkiaEnginePriv;

typedef struct {
    std::unique_ptr<Paragraph> paragraph;
} SkiaTextLayoutPriv;

static TypioTextLayout *skia_create_layout(void *engine, const char *text, const char *font_desc) {
    SkiaEnginePriv *priv = (SkiaEnginePriv *)((TypioTextEngine *)engine)->priv;
    
    ParagraphStyle paragraph_style;
    ParagraphBuilder builder(paragraph_style, priv->font_collection);
    
    TextStyle text_style;
    text_style.setColor(SK_ColorBLACK);
    text_style.setFontFamilies({SkString("sans-serif")});
    text_style.setFontSize(16.0); // Should parse font_desc
    
    builder.pushStyle(text_style);
    builder.addText(text);
    builder.pop();
    
    auto paragraph = builder.Build();
    paragraph->layout(1000); // Max width
    
    SkiaTextLayoutPriv *priv_layout = new SkiaTextLayoutPriv();
    priv_layout->paragraph = std::move(paragraph);
    return (TypioTextLayout *)priv_layout;
}

static void skia_get_metrics(TypioTextLayout *layout, float *out_w, float *out_h) {
    SkiaTextLayoutPriv *priv = (SkiaTextLayoutPriv *)layout;
    *out_w = priv->paragraph->getMaxIntrinsicWidth();
    *out_h = priv->paragraph->getHeight();
}

static void skia_free_layout(TypioTextLayout *layout) {
    SkiaTextLayoutPriv *priv = (SkiaTextLayoutPriv *)layout;
    delete priv;
}

static TypioTextEngineVTable skia_engine_vtable = {
    .create_layout = skia_create_layout,
    .get_metrics = skia_get_metrics,
    .free_layout = skia_free_layout
};

TypioTextEngine *skia_engine_create() {
    TypioTextEngine *engine = (TypioTextEngine *)malloc(sizeof(TypioTextEngine));
    SkiaEnginePriv *priv = new SkiaEnginePriv();
    priv->font_collection = sk_make_sp<FontCollection>();
    priv->font_collection->setDefaultFontManager(SkFontMgr::RefDefault());
    
    engine->priv = priv;
    engine->vtable = &skia_engine_vtable;
    return engine;
}
