/**
 * @file icon_pixmap.cc
 * @brief Pixmap builder for SNI IconPixmap fallback — Skia-drawn icons
 */

#include "icon_pixmap.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkSurface.h"
#include "include/core/SkRRect.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool icon_is_rime(const char *icon_name) {
    return icon_name && strstr(icon_name, "rime") != nullptr;
}

static bool icon_is_mozc(const char *icon_name) {
    return icon_name && strstr(icon_name, "mozc") != nullptr;
}

static bool icon_is_latin_mode(const char *icon_name) {
    return icon_name &&
           (strstr(icon_name, "latin") != nullptr ||
            strstr(icon_name, "direct") != nullptr ||
            strstr(icon_name, "ascii") != nullptr);
}

static bool icon_is_katakana_mode(const char *icon_name) {
    return icon_name && strstr(icon_name, "katakana") != nullptr;
}

static bool icon_is_full_width(const char *icon_name) {
    return icon_name && strstr(icon_name, "full-") != nullptr;
}

static bool icon_is_half_width(const char *icon_name) {
    return icon_name && strstr(icon_name, "half-") != nullptr;
}

/** Draw rounded rectangle helper */
static void draw_rounded_rect(SkCanvas *canvas, float x, float y,
                               float w, float h, float r, const SkPaint &paint) {
    SkRRect rrect;
    rrect.setRectXY(SkRect::MakeXYWH(x, y, w, h), r, r);
    canvas->drawRRect(rrect, paint);
}

static void draw_latin_indicator(SkCanvas *canvas, int size, bool full_width) {
    const float s = (float)size;
    const float cx = s * 0.81f;
    const float cy = s * 0.19f;
    const float r = s * 0.10f;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SkColorSetARGB(255, 232, 168, 56));
    paint.setStyle(SkPaint::kFill_Style);

    if (full_width) {
        float sq = r * 1.4f;
        float rx = sq * 0.3f;
        draw_rounded_rect(canvas, cx - sq, cy - sq, sq * 2.0f, sq * 2.0f, rx, paint);
    } else {
        canvas->drawCircle(cx, cy, r, paint);
    }
}

static void draw_katakana_indicator(SkCanvas *canvas, int size, bool half_width) {
    const float s = (float)size;
    const float cx = s * 0.81f;
    const float cy = s * 0.81f;
    const float r = s * 0.10f;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SkColorSetARGB(255, 61, 122, 207));

    if (half_width) {
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(s * 0.03f);
        canvas->drawCircle(cx, cy, r, paint);
    } else {
        paint.setStyle(SkPaint::kFill_Style);
        canvas->drawCircle(cx, cy, r, paint);
    }
}

static void draw_keyboard_icon(SkCanvas *canvas, int size) {
    const float s = (float)size;
    const float pad = s * 0.14f;
    const float body_w = s - pad * 2.0f;
    const float body_h = s * 0.42f;
    const float body_y = s * 0.29f;
    const float radius = s * 0.08f;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SkColorSetARGB(255, 46, 59, 74));
    paint.setStyle(SkPaint::kFill_Style);

    draw_rounded_rect(canvas, pad, body_y, body_w, body_h, radius, paint);

    paint.setColor(SkColorSetARGB(235, 255, 255, 255));
    const int cols = 5;
    const int rows = 2;
    const float key_w = body_w * 0.12f;
    const float key_h = body_h * 0.16f;
    const float gap_x = body_w * 0.06f;
    const float gap_y = body_h * 0.16f;

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            float x = pad + body_w * 0.10f + (float)col * (key_w + gap_x);
            float y = body_y + body_h * 0.18f + (float)row * (key_h + gap_y);
            canvas->drawRect(SkRect::MakeXYWH(x, y, key_w, key_h), paint);
        }
    }

    canvas->drawRect(SkRect::MakeXYWH(pad + body_w * 0.26f, body_y + body_h * 0.68f,
                                      body_w * 0.48f, key_h), paint);
}

static void draw_rime_icon(SkCanvas *canvas, int size) {
    const float s = (float)size;
    const float pad = s * 0.03125f;
    const float side = s * 0.9375f;
    const float radius = s * 0.1875f;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SkColorSetARGB(255, 61, 90, 128));
    paint.setStyle(SkPaint::kFill_Style);
    draw_rounded_rect(canvas, pad, pad, side, side, radius, paint);

    paint.setColor(SK_ColorWHITE);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(s * 0.0625f);
    paint.setStrokeCap(SkPaint::kRound_Cap);
    paint.setStrokeJoin(SkPaint::kRound_Join);

    SkPathBuilder pb;
    pb.moveTo(s * 0.328f, s * 0.781f);
    pb.lineTo(s * 0.328f, s * 0.219f);
    pb.lineTo(s * 0.5625f, s * 0.219f);
    pb.cubicTo(s * 0.703f, s * 0.219f,
               s * 0.703f, s * 0.5f,
               s * 0.578f, s * 0.531f);
    canvas->drawPath(pb.detach(), paint);

    canvas->drawLine(s * 0.578f, s * 0.531f, s * 0.719f, s * 0.781f, paint);
}

static void draw_mozc_icon(SkCanvas *canvas, int size) {
    const float s = (float)size;
    const float pad = s * 0.03125f;
    const float side = s * 0.9375f;
    const float radius = s * 0.1875f;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SkColorSetARGB(255, 181, 69, 58));
    paint.setStyle(SkPaint::kFill_Style);
    draw_rounded_rect(canvas, pad, pad, side, side, radius, paint);

    paint.setColor(SK_ColorWHITE);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(s * 0.0625f);
    paint.setStrokeCap(SkPaint::kRound_Cap);
    paint.setStrokeJoin(SkPaint::kRound_Join);

    SkPathBuilder pb;
    pb.moveTo(s * 0.266f, s * 0.781f);
    pb.lineTo(s * 0.328f, s * 0.219f);
    pb.lineTo(s * 0.5f,   s * 0.5625f);
    pb.lineTo(s * 0.672f, s * 0.219f);
    pb.lineTo(s * 0.734f, s * 0.781f);
    canvas->drawPath(pb.detach(), paint);
}

bool typio_tray_icon_pixmap_build(const char *icon_name, int preferred_size,
                                  int *width_out, int *height_out,
                                  unsigned char **data_out, int *data_len_out) {
    if (!width_out || !height_out || !data_out || !data_len_out) {
        return false;
    }

    int size = preferred_size > 0 ? preferred_size : 64;
    SkImageInfo info = SkImageInfo::Make(size, size, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) {
        return false;
    }

    SkCanvas *canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    if (icon_is_rime(icon_name)) {
        draw_rime_icon(canvas, size);
    } else if (icon_is_mozc(icon_name)) {
        draw_mozc_icon(canvas, size);
    } else {
        draw_keyboard_icon(canvas, size);
    }

    if (icon_is_latin_mode(icon_name)) {
        draw_latin_indicator(canvas, size, icon_is_full_width(icon_name));
    }
    if (icon_is_katakana_mode(icon_name)) {
        draw_katakana_indicator(canvas, size, icon_is_half_width(icon_name));
    }

    unsigned char *argb = (unsigned char *)calloc((size_t)size * (size_t)size * 4U, sizeof(unsigned char));
    if (!argb) {
        return false;
    }

    uint32_t *pixels = (uint32_t *)malloc((size_t)size * (size_t)size * 4U);
    if (!pixels) {
        free(argb);
        return false;
    }

    SkPixmap pixmap(info, pixels, (size_t)size * 4U);
    if (!surface->readPixels(pixmap, 0, 0)) {
        free(pixels);
        free(argb);
        return false;
    }

    for (int i = 0; i < size * size; ++i) {
        SkColor c = pixmap.getColor(i % size, i / size);
        argb[i * 4 + 0] = (unsigned char)SkColorGetA(c);
        argb[i * 4 + 1] = (unsigned char)SkColorGetR(c);
        argb[i * 4 + 2] = (unsigned char)SkColorGetG(c);
        argb[i * 4 + 3] = (unsigned char)SkColorGetB(c);
    }

    free(pixels);
    *data_out = argb;
    *data_len_out = size * size * 4;
    *width_out = size;
    *height_out = size;
    return true;
}

void typio_tray_icon_pixmap_free(unsigned char *data) {
    free(data);
}
