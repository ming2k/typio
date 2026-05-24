/**
 * @file icon_pixmap.cc
 * @brief Pixmap builder for SNI IconPixmap fallback.
 */

#include "icon_pixmap.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char *data;
    int size;
} Pixmap;

typedef struct {
    unsigned char a;
    unsigned char r;
    unsigned char g;
    unsigned char b;
} Color;

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

static void blend_pixel(Pixmap *pm, int x, int y, Color c) {
    if (!pm || !pm->data || x < 0 || y < 0 || x >= pm->size || y >= pm->size) return;

    unsigned char *dst = &pm->data[((size_t)y * (size_t)pm->size + (size_t)x) * 4U];
    unsigned int inv = 255U - c.a;
    dst[1] = (unsigned char)(((unsigned int)c.r * c.a + (unsigned int)dst[1] * inv) / 255U);
    dst[2] = (unsigned char)(((unsigned int)c.g * c.a + (unsigned int)dst[2] * inv) / 255U);
    dst[3] = (unsigned char)(((unsigned int)c.b * c.a + (unsigned int)dst[3] * inv) / 255U);
    dst[0] = (unsigned char)(c.a + ((unsigned int)dst[0] * inv) / 255U);
}

static void fill_rect(Pixmap *pm, float x, float y, float w, float h, Color c) {
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = (int)ceilf(x + w);
    int y1 = (int)ceilf(y + h);

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            blend_pixel(pm, px, py, c);
        }
    }
}

static void fill_rounded_rect(Pixmap *pm, float x, float y, float w, float h,
                              float r, Color c) {
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = (int)ceilf(x + w);
    int y1 = (int)ceilf(y + h);
    float right = x + w;
    float bottom = y + h;

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            float cx = (float)px + 0.5f;
            float cy = (float)py + 0.5f;
            float nearest_x = cx;
            float nearest_y = cy;
            if (cx < x + r) nearest_x = x + r;
            if (cx > right - r) nearest_x = right - r;
            if (cy < y + r) nearest_y = y + r;
            if (cy > bottom - r) nearest_y = bottom - r;
            float dx = cx - nearest_x;
            float dy = cy - nearest_y;
            if (dx * dx + dy * dy <= r * r) blend_pixel(pm, px, py, c);
        }
    }
}

static void fill_circle(Pixmap *pm, float cx, float cy, float r, Color c) {
    int x0 = (int)floorf(cx - r);
    int y0 = (int)floorf(cy - r);
    int x1 = (int)ceilf(cx + r);
    int y1 = (int)ceilf(cy + r);
    float rr = r * r;

    for (int py = y0; py <= y1; ++py) {
        for (int px = x0; px <= x1; ++px) {
            float dx = (float)px + 0.5f - cx;
            float dy = (float)py + 0.5f - cy;
            if (dx * dx + dy * dy <= rr) blend_pixel(pm, px, py, c);
        }
    }
}

static void stroke_circle(Pixmap *pm, float cx, float cy, float r, float width, Color c) {
    int x0 = (int)floorf(cx - r - width);
    int y0 = (int)floorf(cy - r - width);
    int x1 = (int)ceilf(cx + r + width);
    int y1 = (int)ceilf(cy + r + width);
    float half = width * 0.5f;

    for (int py = y0; py <= y1; ++py) {
        for (int px = x0; px <= x1; ++px) {
            float dx = (float)px + 0.5f - cx;
            float dy = (float)py + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            if (fabsf(d - r) <= half) blend_pixel(pm, px, py, c);
        }
    }
}

static void stroke_line(Pixmap *pm, float x0, float y0, float x1, float y1,
                        float width, Color c) {
    float min_x = fminf(x0, x1) - width;
    float max_x = fmaxf(x0, x1) + width;
    float min_y = fminf(y0, y1) - width;
    float max_y = fmaxf(y0, y1) + width;
    float vx = x1 - x0;
    float vy = y1 - y0;
    float len2 = vx * vx + vy * vy;
    float half = width * 0.5f;

    for (int py = (int)floorf(min_y); py <= (int)ceilf(max_y); ++py) {
        for (int px = (int)floorf(min_x); px <= (int)ceilf(max_x); ++px) {
            float wx = (float)px + 0.5f - x0;
            float wy = (float)py + 0.5f - y0;
            float t = len2 > 0.0f ? (wx * vx + wy * vy) / len2 : 0.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float cx = x0 + t * vx;
            float cy = y0 + t * vy;
            float dx = (float)px + 0.5f - cx;
            float dy = (float)py + 0.5f - cy;
            if (dx * dx + dy * dy <= half * half) blend_pixel(pm, px, py, c);
        }
    }
}

static void draw_latin_indicator(Pixmap *pm, int size, bool full_width) {
    const float s = (float)size;
    const float cx = s * 0.81f;
    const float cy = s * 0.19f;
    const float r = s * 0.10f;
    Color color = {255, 232, 168, 56};

    if (full_width) {
        float sq = r * 1.4f;
        fill_rounded_rect(pm, cx - sq, cy - sq, sq * 2.0f, sq * 2.0f,
                          sq * 0.3f, color);
    } else {
        fill_circle(pm, cx, cy, r, color);
    }
}

static void draw_katakana_indicator(Pixmap *pm, int size, bool half_width) {
    const float s = (float)size;
    const float cx = s * 0.81f;
    const float cy = s * 0.81f;
    const float r = s * 0.10f;
    Color color = {255, 61, 122, 207};

    if (half_width) {
        stroke_circle(pm, cx, cy, r, s * 0.03f, color);
    } else {
        fill_circle(pm, cx, cy, r, color);
    }
}

static void draw_keyboard_icon(Pixmap *pm, int size) {
    const float s = (float)size;
    const float pad = s * 0.14f;
    const float body_w = s - pad * 2.0f;
    const float body_h = s * 0.42f;
    const float body_y = s * 0.29f;
    Color body = {255, 46, 59, 74};
    Color key = {235, 255, 255, 255};

    fill_rounded_rect(pm, pad, body_y, body_w, body_h, s * 0.08f, body);

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
            fill_rect(pm, x, y, key_w, key_h, key);
        }
    }

    fill_rect(pm, pad + body_w * 0.26f, body_y + body_h * 0.68f,
              body_w * 0.48f, key_h, key);
}

static void draw_rime_icon(Pixmap *pm, int size) {
    const float s = (float)size;
    Color bg = {255, 61, 90, 128};
    Color fg = {255, 255, 255, 255};
    float stroke = s * 0.0625f;

    fill_rounded_rect(pm, s * 0.03125f, s * 0.03125f,
                      s * 0.9375f, s * 0.9375f, s * 0.1875f, bg);
    stroke_line(pm, s * 0.328f, s * 0.781f, s * 0.328f, s * 0.219f, stroke, fg);
    stroke_line(pm, s * 0.328f, s * 0.219f, s * 0.5625f, s * 0.219f, stroke, fg);
    stroke_line(pm, s * 0.5625f, s * 0.219f, s * 0.63f, s * 0.33f, stroke, fg);
    stroke_line(pm, s * 0.63f, s * 0.33f, s * 0.578f, s * 0.531f, stroke, fg);
    stroke_line(pm, s * 0.578f, s * 0.531f, s * 0.719f, s * 0.781f, stroke, fg);
}

static void draw_mozc_icon(Pixmap *pm, int size) {
    const float s = (float)size;
    Color bg = {255, 181, 69, 58};
    Color fg = {255, 255, 255, 255};
    float stroke = s * 0.0625f;

    fill_rounded_rect(pm, s * 0.03125f, s * 0.03125f,
                      s * 0.9375f, s * 0.9375f, s * 0.1875f, bg);
    stroke_line(pm, s * 0.266f, s * 0.781f, s * 0.328f, s * 0.219f, stroke, fg);
    stroke_line(pm, s * 0.328f, s * 0.219f, s * 0.5f, s * 0.5625f, stroke, fg);
    stroke_line(pm, s * 0.5f, s * 0.5625f, s * 0.672f, s * 0.219f, stroke, fg);
    stroke_line(pm, s * 0.672f, s * 0.219f, s * 0.734f, s * 0.781f, stroke, fg);
}

bool typio_tray_icon_pixmap_build(const char *icon_name, int preferred_size,
                                  int *width_out, int *height_out,
                                  unsigned char **data_out, int *data_len_out) {
    if (!width_out || !height_out || !data_out || !data_len_out) {
        return false;
    }

    int size = preferred_size > 0 ? preferred_size : 64;
    unsigned char *argb = (unsigned char *)calloc((size_t)size * (size_t)size * 4U,
                                                  sizeof(unsigned char));
    if (!argb) return false;

    Pixmap pm = {argb, size};
    if (icon_is_rime(icon_name)) {
        draw_rime_icon(&pm, size);
    } else if (icon_is_mozc(icon_name)) {
        draw_mozc_icon(&pm, size);
    } else {
        draw_keyboard_icon(&pm, size);
    }

    if (icon_is_latin_mode(icon_name)) {
        draw_latin_indicator(&pm, size, icon_is_full_width(icon_name));
    }
    if (icon_is_katakana_mode(icon_name)) {
        draw_katakana_indicator(&pm, size, icon_is_half_width(icon_name));
    }

    *data_out = argb;
    *data_len_out = size * size * 4;
    *width_out = size;
    *height_out = size;
    return true;
}

void typio_tray_icon_pixmap_free(unsigned char *data) {
    free(data);
}
