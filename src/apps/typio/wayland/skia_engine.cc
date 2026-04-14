#include "typio/renderer.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkRect.h"
#include "include/core/SkPaint.h"
#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/TypefaceFontProvider.h"
#include "modules/skunicode/include/SkUnicode_icu.h"
#include <stdlib.h>

using namespace skia::textlayout;

typedef struct {
    sk_sp<FontCollection> font_collection;
    sk_sp<SkUnicode> unicode;
} SkiaEnginePriv;

static TypioTextLayout *skia_create_layout(void *engine, const char *text, const char *font_desc) {
    SkiaEnginePriv *priv = (SkiaEnginePriv *)((TypioTextEngine *)engine)->priv;

    /* Parse font_desc: "Family [Style] Size" */
    char family[128] = "sans-serif";
    float size = 16.0f;
    if (font_desc && font_desc[0]) {
        const char *last_space = strrchr(font_desc, ' ');
        if (last_space) {
            size = (float)atof(last_space + 1);
            size_t fam_len = (size_t)(last_space - font_desc);
            if (fam_len > sizeof(family) - 1) fam_len = sizeof(family) - 1;
            memcpy(family, font_desc, fam_len);
            family[fam_len] = '\0';
        } else {
            size = (float)atof(font_desc);
            if (size <= 0) {
                snprintf(family, sizeof(family), "%s", font_desc);
                size = 16.0f;
            }
        }
    }

    /* Convert points to pixels. Typio font descriptors use points.
       Skia expects pixels. At 96 DPI, 1 point = 1.333 pixels. */
    size = size * (96.0f / 72.0f);

    TextStyle text_style;
    text_style.setColor(SK_ColorBLACK);
    text_style.setFontFamilies({SkString(family)});
    text_style.setFontSize(size);

    ParagraphStyle paragraph_style;
    paragraph_style.setTextStyle(text_style);

    auto builder = ParagraphBuilder::make(paragraph_style, priv->font_collection, priv->unicode);

    builder->pushStyle(text_style);
    builder->addText(text);
    builder->pop();

    auto paragraph = builder->Build();
    paragraph->layout(10000); // High limit for measurement

    return (TypioTextLayout *)paragraph.release();
}

static void skia_get_metrics(TypioTextLayout *layout, float *out_w, float *out_h) {
    Paragraph *para = (Paragraph *)layout;
    *out_w = para->getMaxIntrinsicWidth();
    *out_h = para->getHeight();
}

static float skia_get_baseline(TypioTextLayout *layout) {
    return ((Paragraph *)layout)->getAlphabeticBaseline();
}

static void skia_free_layout(TypioTextLayout *layout) {
    delete (Paragraph *)layout;
}

static TypioTextEngineVTable skia_engine_vtable = {
    .create_layout = skia_create_layout,
    .get_metrics   = skia_get_metrics,
    .get_baseline  = skia_get_baseline,
    .free_layout   = skia_free_layout,
};

extern "C" TypioTextEngine *skia_engine_create() {
    TypioTextEngine *engine = (TypioTextEngine *)malloc(sizeof(TypioTextEngine));
    SkiaEnginePriv *priv = new SkiaEnginePriv();
    priv->font_collection = sk_make_sp<FontCollection>();
    priv->font_collection->setDefaultFontManager(
        SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType()));
    priv->unicode = SkUnicodes::ICU::Make();

    engine->priv = priv;
    engine->vtable = &skia_engine_vtable;
    return engine;
}
