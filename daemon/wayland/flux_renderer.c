#include "flux_renderer.h"

#include <flux/flux.h>

#include <fontconfig/fontconfig.h>
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <flux/canvas.h>

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    uint32_t glyph_id;   /* FreeType face glyph index */
    float    x;          /* pen x at layout creation */
    float    y_offset;   /* y offset from HarfBuzz positioning */
} GlyphEntry;

struct TypioTextLayout {
    GlyphEntry *glyphs;
    size_t      count;
    FT_Face     face;     /* borrowed; valid while font cache holds it */
    TypioColor  color;
    float       width;
    float       height;
    float       baseline;

    /* Cached premultiplied-RGBA glyph image, built lazily on first draw and
     * reused for the layout's lifetime (the layout itself is LRU-cached). */
    flux_image *image;
    int         img_w, img_h;
    float       img_dx, img_dy;  /* image top-left offset from layout origin */
    bool        image_attempted;
};

static flux_device *global_device;
static FT_Library   ft_library;

static bool ensure_ft_library(void)
{
    if (ft_library) return true;
    if (FT_Init_FreeType(&ft_library) != 0) {
        ft_library = NULL;
        return false;
    }
    return true;
}

/* ── Font file cache ────────────────────────────────────────────────── */
#define FONT_FILE_CACHE_CAP 8

typedef struct {
    char    family[128];
    int32_t weight;
    char   *path;
} FontFileEntry;

static FontFileEntry font_file_cache[FONT_FILE_CACHE_CAP];
static size_t        font_file_cache_count = 0;

static void font_file_cache_clear(void)
{
    for (size_t i = 0; i < font_file_cache_count; ++i) {
        free(font_file_cache[i].path);
        font_file_cache[i].path = NULL;
        font_file_cache[i].family[0] = '\0';
        font_file_cache[i].weight = 400;
    }
    font_file_cache_count = 0;
}

static char *font_file_cache_lookup(const char *family, int32_t weight)
{
    for (size_t i = 0; i < font_file_cache_count; ++i) {
        if (font_file_cache[i].weight == weight &&
            strcmp(font_file_cache[i].family, family) == 0) {
            return strdup(font_file_cache[i].path);
        }
    }
    return NULL;
}

static void font_file_cache_insert(const char *family, int32_t weight, const char *path)
{
    if (font_file_cache_count < FONT_FILE_CACHE_CAP) {
        FontFileEntry *e = &font_file_cache[font_file_cache_count++];
        snprintf(e->family, sizeof(e->family), "%s", family);
        e->weight = weight;
        e->path = strdup(path);
    } else {
        free(font_file_cache[0].path);
        for (size_t i = 1; i < FONT_FILE_CACHE_CAP; ++i)
            font_file_cache[i - 1] = font_file_cache[i];
        FontFileEntry *e = &font_file_cache[FONT_FILE_CACHE_CAP - 1];
        snprintf(e->family, sizeof(e->family), "%s", family);
        e->weight = weight;
        e->path = strdup(path);
    }
}

/* ── Font object cache (FT_Face + hb_font_t) ────────────────────────── */
#define FONT_OBJ_CACHE_CAP 16

typedef struct {
    char      *path;
    float      size;
    int32_t    weight;
    FT_Face    face;
    hb_font_t *hb_font;
    uint32_t   font_id;
} FontObjEntry;

static FontObjEntry font_obj_cache[FONT_OBJ_CACHE_CAP];
static size_t       font_obj_cache_count = 0;
static uint32_t     next_font_id = 1;

static void font_obj_cache_clear(void)
{
    for (size_t i = 0; i < font_obj_cache_count; ++i) {
        if (font_obj_cache[i].hb_font)
            hb_font_destroy(font_obj_cache[i].hb_font);
        if (font_obj_cache[i].face)
            FT_Done_Face(font_obj_cache[i].face);
        free(font_obj_cache[i].path);
    }
    font_obj_cache_count = 0;
}

static FontObjEntry *font_obj_cache_lookup(const char *path, float size, int32_t weight)
{
    for (size_t i = 0; i < font_obj_cache_count; ++i) {
        if (font_obj_cache[i].size == size &&
            font_obj_cache[i].weight == weight &&
            strcmp(font_obj_cache[i].path, path) == 0) {
            return &font_obj_cache[i];
        }
    }
    return NULL;
}

static void font_obj_cache_insert(const char *path, float size, int32_t weight,
                                  FT_Face face, hb_font_t *hb_font, uint32_t font_id)
{
    if (font_obj_cache_count < FONT_OBJ_CACHE_CAP) {
        FontObjEntry *e = &font_obj_cache[font_obj_cache_count++];
        e->path = strdup(path);
        e->size = size;
        e->weight = weight;
        e->face = face;
        e->hb_font = hb_font;
        e->font_id = font_id;
    } else {
        free(font_obj_cache[0].path);
        if (font_obj_cache[0].hb_font)
            hb_font_destroy(font_obj_cache[0].hb_font);
        if (font_obj_cache[0].face)
            FT_Done_Face(font_obj_cache[0].face);
        for (size_t i = 1; i < FONT_OBJ_CACHE_CAP; ++i)
            font_obj_cache[i - 1] = font_obj_cache[i];
        FontObjEntry *e = &font_obj_cache[FONT_OBJ_CACHE_CAP - 1];
        e->path = strdup(path);
        e->size = size;
        e->weight = weight;
        e->face = face;
        e->hb_font = hb_font;
        e->font_id = font_id;
    }
}

static bool set_face_weight(FT_Face face, int32_t weight)
{
    FT_MM_Var *amaster = NULL;
    FT_Fixed  *coords  = NULL;
    FT_Error   err;
    FT_UInt    i;
    bool       ok = false;

    err = FT_Get_MM_Var(face, &amaster);
    if (err != 0) return false;

    coords = (FT_Fixed *)calloc(amaster->num_axis, sizeof(FT_Fixed));
    if (!coords) goto done;

    err = FT_Get_Var_Design_Coordinates(face, amaster->num_axis, coords);
    if (err != 0) goto done;

    for (i = 0; i < amaster->num_axis; ++i) {
        if (amaster->axis[i].tag == ((FT_ULong)'w' << 24 |
                                     (FT_ULong)'g' << 16 |
                                     (FT_ULong)'h' << 8  | 't')) {
            coords[i] = (FT_Fixed)weight * 65536;
            ok = true;
            break;
        }
    }

    if (ok) {
        err = FT_Set_Var_Design_Coordinates(face, amaster->num_axis, coords);
        ok = (err == 0);
    }

done:
    free(coords);
    FT_Done_MM_Var(ft_library, amaster);
    return ok;
}

static FontObjEntry *get_or_create_font(const char *path, float size, int32_t weight)
{
    FontObjEntry *entry = font_obj_cache_lookup(path, size, weight);
    if (entry) return entry;

    FT_Face face = NULL;
    if (FT_New_Face(ft_library, path, 0, &face) != 0) return NULL;

    set_face_weight(face, weight);

    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)(size + 0.5f)) != 0) {
        FT_Done_Face(face);
        return NULL;
    }

    hb_font_t *hb_font = hb_ft_font_create_referenced(face);
    if (!hb_font) {
        FT_Done_Face(face);
        return NULL;
    }

    uint32_t font_id = next_font_id++;
    font_obj_cache_insert(path, size, weight, face, hb_font, font_id);
    return font_obj_cache_lookup(path, size, weight);
}

/* ── Helpers ────────────────────────────────────────────────────────── */

static unsigned char to_u8(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (unsigned char)(v * 255.0f + 0.5f);
}

static void flux_log_cb(flux_log_level level,
                        const char *file, int line,
                        const char *fmt, const char *msg,
                        void *user)
{
    (void)level; (void)file; (void)line; (void)fmt; (void)msg; (void)user;
}

flux_device *typio_flux_device_get(void)
{
    if (global_device) return global_device;

    /* Wayland WSI: the candidate popup presents a swapchain directly onto
     * its zwp_input_popup_surface_v2 wl_surface, so the device needs the
     * surface + wayland-surface instance extensions and the swapchain
     * device extension. The graphics queue is assumed present-capable
     * (true on Mesa/Wayland). */
    static const char *const instance_exts[] = {
        "VK_KHR_surface",
        "VK_KHR_wayland_surface",
    };
    static const char *const device_exts[] = {
        "VK_KHR_swapchain",
    };

    flux_device_desc desc = FLUX_DEVICE_DESC_INIT;
    desc.log                               = flux_log_cb;
    desc.headless                          = false;
    desc.required_instance_extensions      = instance_exts;
    desc.required_instance_extension_count = 2;
    desc.required_device_extensions        = device_exts;
    desc.required_device_extension_count   = 1;
    desc.frames_in_flight                  = 2;

    if (flux_device_create(&desc, &global_device) != FLUX_OK) {
        global_device = NULL;
        return NULL;
    }
    return global_device;
}

static int32_t parse_weight_keyword(const char *s, size_t len)
{
    if (len == 6 && strncasecmp(s, "Medium", 6) == 0)      return 500;
    if (len == 4 && strncasecmp(s, "Bold", 4) == 0)        return 700;
    if (len == 6 && strncasecmp(s, "Normal", 6) == 0)      return 400;
    if (len == 7 && strncasecmp(s, "Regular", 7) == 0)     return 400;
    if (len == 5 && strncasecmp(s, "Light", 5) == 0)       return 300;
    if (len == 4 && strncasecmp(s, "Thin", 4) == 0)        return 100;
    if (len == 9 && strncasecmp(s, "ExtraBold", 9) == 0)   return 800;
    if (len == 5 && strncasecmp(s, "Black", 5) == 0)       return 900;
    if (len == 8 && strncasecmp(s, "SemiBold", 8) == 0)    return 600;
    if (len == 10 && strncasecmp(s, "ExtraLight", 10) == 0) return 200;
    {
        int v = atoi(s);
        if (v >= 100 && v <= 1000) return v;
    }
    return 0;
}

static bool parse_font_desc(const char *font_desc,
                            char *family,
                            size_t family_size,
                            float *size,
                            int32_t *weight)
{
    if (!family || family_size == 0 || !size) return false;

    snprintf(family, family_size, "Sans");
    *size = 16.0f;
    if (weight) *weight = 400;

    if (!font_desc || !font_desc[0]) return true;

    const char *last_space = strrchr(font_desc, ' ');
    if (!last_space || !last_space[1]) {
        snprintf(family, family_size, "%s", font_desc);
        return true;
    }

    float parsed = (float)atof(last_space + 1);
    if (parsed <= 0.0f) {
        snprintf(family, family_size, "%s", font_desc);
        return true;
    }
    *size = parsed * (96.0f / 72.0f);

    const char *family_end = last_space;

    if (last_space > font_desc) {
        const char *p = last_space - 1;
        while (p > font_desc && *p != ' ') p--;
        if (*p == ' ') {
            const char *wstart = p + 1;
            size_t wlen = (size_t)(last_space - wstart);
            int32_t w = parse_weight_keyword(wstart, wlen);
            if (w > 0) {
                if (weight) *weight = w;
                family_end = p;
            }
        }
    }

    size_t flen = (size_t)(family_end - font_desc);
    if (flen >= family_size) flen = family_size - 1;
    memcpy(family, font_desc, flen);
    family[flen] = '\0';
    return true;
}

static char *match_font_file(const char *family, int32_t weight)
{
    char *cached = font_file_cache_lookup(family, weight);
    if (cached) return cached;

    if (!FcInit()) return NULL;

    FcPattern *pat = FcPatternCreate();
    if (!pat) return NULL;

    FcPatternAddString(pat, FC_FAMILY,
                       (const FcChar8 *)(family && family[0] ? family : "Sans"));
    int fc_weight = FC_WEIGHT_REGULAR;
    if (weight >= 900)      fc_weight = FC_WEIGHT_BLACK;
    else if (weight >= 800) fc_weight = FC_WEIGHT_EXTRABOLD;
    else if (weight >= 700) fc_weight = FC_WEIGHT_BOLD;
    else if (weight >= 600) fc_weight = FC_WEIGHT_DEMIBOLD;
    else if (weight >= 500) fc_weight = FC_WEIGHT_MEDIUM;
    else if (weight >= 400) fc_weight = FC_WEIGHT_REGULAR;
    else if (weight >= 300) fc_weight = FC_WEIGHT_LIGHT;
    else if (weight >= 200) fc_weight = FC_WEIGHT_EXTRALIGHT;
    else                    fc_weight = FC_WEIGHT_THIN;
    FcPatternAddInteger(pat, FC_WEIGHT, fc_weight);
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult fc_result;
    FcPattern *match = FcFontMatch(NULL, pat, &fc_result);
    char *result = NULL;
    if (match) {
        FcChar8 *file = NULL;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
            result = strdup((const char *)file);
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pat);

    if (result) {
        font_file_cache_insert(family, weight, result);
    }
    return result;
}

static bool text_has_non_ascii(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        if (*p > 127) return true;
        p++;
    }
    return false;
}

static char *find_fallback_font(const char *text, int32_t weight)
{
    if (!text || !text[0]) return NULL;
    if (!text_has_non_ascii(text)) return NULL;
    if (!FcInit()) return NULL;

    FcPattern *pat = FcPatternCreate();
    if (!pat) return NULL;

    FcCharSet *cs = FcCharSetCreate();
    const char *p = text;
    while (*p) {
        FcChar32 ch;
        int len = FcUtf8ToUcs4((const FcChar8 *)p, &ch, (int)strlen(p));
        if (len <= 0) break;
        FcCharSetAddChar(cs, ch);
        p += len;
    }

    int fc_weight = FC_WEIGHT_REGULAR;
    if (weight >= 900)      fc_weight = FC_WEIGHT_BLACK;
    else if (weight >= 800) fc_weight = FC_WEIGHT_EXTRABOLD;
    else if (weight >= 700) fc_weight = FC_WEIGHT_BOLD;
    else if (weight >= 600) fc_weight = FC_WEIGHT_DEMIBOLD;
    else if (weight >= 500) fc_weight = FC_WEIGHT_MEDIUM;
    else if (weight >= 400) fc_weight = FC_WEIGHT_REGULAR;
    else if (weight >= 300) fc_weight = FC_WEIGHT_LIGHT;
    else if (weight >= 200) fc_weight = FC_WEIGHT_EXTRALIGHT;
    else                    fc_weight = FC_WEIGHT_THIN;
    FcPatternAddInteger(pat, FC_WEIGHT, fc_weight);
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult fc_result;
    FcFontSet *set = FcFontSort(NULL, pat, FcTrue, NULL, &fc_result);
    char *result = NULL;

    if (set) {
        for (int i = 0; i < set->nfont; i++) {
            FcPattern *font = set->fonts[i];
            FcCharSet *font_cs = NULL;
            if (FcPatternGetCharSet(font, FC_CHARSET, 0, &font_cs) == FcResultMatch && font_cs) {
                bool covers_all = true;
                const char *cp = text;
                while (*cp) {
                    FcChar32 ch;
                    int len = FcUtf8ToUcs4((const FcChar8 *)cp, &ch, (int)strlen(cp));
                    if (len <= 0) break;
                    if (!FcCharSetHasChar(font_cs, ch)) {
                        covers_all = false;
                        break;
                    }
                    cp += len;
                }
                if (covers_all) {
                    FcChar8 *file = NULL;
                    if (FcPatternGetString(font, FC_FILE, 0, &file) == FcResultMatch && file) {
                        result = strdup((const char *)file);
                        break;
                    }
                }
            }
        }
        FcFontSetDestroy(set);
    }

    FcCharSetDestroy(cs);
    FcPatternDestroy(pat);
    return result;
}

static bool layout_has_missing_glyphs(const TypioTextLayout *layout)
{
    if (!layout || !layout->glyphs) return false;
    for (size_t i = 0; i < layout->count; ++i) {
        /* HarfBuzz glyph ID 0 is .notdef — missing glyph */
        if (layout->glyphs[i].glyph_id == 0) return true;
    }
    return false;
}

static void flux_free_layout_internal(TypioTextLayout *layout)
{
    if (!layout) return;
    if (layout->image) flux_image_release(layout->image);
    free(layout->glyphs);
    free(layout);
}

static TypioTextLayout *flux_shape_text(FontObjEntry *font,
                                        const char *text,
                                        TypioColor color)
{
    TypioTextLayout *layout = (TypioTextLayout *)calloc(1, sizeof(*layout));
    if (!layout) return NULL;

    layout->color = color;
    layout->face  = font->face;

    {
        float ascender  = (float)font->face->size->metrics.ascender  / 64.0f;
        float descender = (float)font->face->size->metrics.descender / 64.0f;
        layout->baseline = ascender;
        layout->height   = ascender - descender;
    }

    hb_buffer_t *hb = hb_buffer_create();
    if (!hb) goto fail;
    hb_buffer_add_utf8(hb, text ? text : "", -1, 0, -1);
    hb_buffer_guess_segment_properties(hb);
    hb_shape(font->hb_font, hb, NULL, 0);

    unsigned int count = 0;
    hb_glyph_info_t     *infos     = hb_buffer_get_glyph_infos(hb, &count);
    hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(hb, &count);

    if (count > 0) {
        layout->glyphs = (GlyphEntry *)calloc(count, sizeof(GlyphEntry));
        if (!layout->glyphs) { hb_buffer_destroy(hb); goto fail; }
    }
    layout->count = count;

    float pen_x = 0.0f;
    for (unsigned int i = 0; i < count; ++i) {
        layout->glyphs[i].glyph_id = infos[i].codepoint;
        layout->glyphs[i].x        = pen_x + (float)positions[i].x_offset / 64.0f;
        layout->glyphs[i].y_offset = -(float)positions[i].y_offset / 64.0f;
        pen_x += (float)positions[i].x_advance / 64.0f;
    }
    layout->width = pen_x;
    hb_buffer_destroy(hb);
    return layout;

fail:
    flux_free_layout_internal(layout);
    return NULL;
}

static TypioTextLayout *flux_create_layout(void *engine,
                                           const char *text,
                                           const char *font_desc,
                                           TypioColor color)
{
    char family[128];
    char *font_file = NULL;
    char *fb_file   = NULL;
    float size_px;
    FontObjEntry *font = NULL;
    TypioTextLayout *layout    = NULL;
    TypioTextLayout *fb_layout = NULL;
    int32_t weight = 400;

    (void)engine;
    if (!parse_font_desc(font_desc, family, sizeof(family), &size_px, &weight)) return NULL;

    font_file = match_font_file(family, weight);
    if (!font_file) return NULL;

    font = get_or_create_font(font_file, size_px, weight);
    if (!font) goto fail;

    layout = flux_shape_text(font, text, color);
    if (!layout) goto fail;

    if (layout_has_missing_glyphs(layout)) {
        fb_file = find_fallback_font(text, weight);
        if (fb_file && strcmp(fb_file, font_file) != 0) {
            FontObjEntry *fb_font = get_or_create_font(fb_file, size_px, weight);
            if (fb_font) {
                fb_layout = flux_shape_text(fb_font, text, color);
                if (fb_layout && !layout_has_missing_glyphs(fb_layout)) {
                    flux_free_layout_internal(layout);
                    layout    = fb_layout;
                    fb_layout = NULL;
                } else {
                    flux_free_layout_internal(fb_layout);
                    fb_layout = NULL;
                }
            }
        }
        free(fb_file);
    }

    free(font_file);
    return layout;

fail:
    free(font_file);
    flux_free_layout_internal(layout);
    flux_free_layout_internal(fb_layout);
    return NULL;
}

static void flux_get_metrics(TypioTextLayout *layout, float *out_w, float *out_h)
{
    if (out_w) *out_w = layout ? layout->width    : 0.0f;
    if (out_h) *out_h = layout ? layout->height   : 0.0f;
}

static float flux_get_baseline(TypioTextLayout *layout)
{
    return layout ? layout->baseline : 0.0f;
}

void typio_flux_layout_free(TypioTextLayout *layout)
{
    flux_free_layout_internal(layout);
}

/* ── Glyph rasterisation → cached flux_image ────────────────────────── */

/* Rasterise every glyph in the layout into one premultiplied-RGBA image and
 * cache it on the layout. FreeType produces 8-bit coverage bitmaps; we tint
 * by the layout colour and premultiply so flux's default SRC_OVER composites
 * correctly. Built once per layout (layouts are LRU-cached). */
static bool build_layout_image(TypioTextLayout *layout)
{
    layout->image_attempted = true;

    if (!layout->face || !layout->glyphs || layout->count == 0) return false;

    flux_device *dev = typio_flux_device_get();
    if (!dev) return false;

    FT_Face face = layout->face;
    int minx = INT_MAX, miny = INT_MAX, maxx = INT_MIN, maxy = INT_MIN;

    /* Pass 1: ink bounding box (in layout space, origin at top, baseline below). */
    for (size_t i = 0; i < layout->count; ++i) {
        if (FT_Load_Glyph(face, layout->glyphs[i].glyph_id,
                          FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
            continue;
        FT_GlyphSlot s = face->glyph;
        FT_Bitmap   *b = &s->bitmap;
        if (b->width == 0 || b->rows == 0) continue;

        int gx = (int)floorf(layout->glyphs[i].x) + s->bitmap_left;
        int gy = (int)floorf(layout->baseline + layout->glyphs[i].y_offset) - s->bitmap_top;
        if (gx < minx) minx = gx;
        if (gy < miny) miny = gy;
        if (gx + (int)b->width > maxx) maxx = gx + (int)b->width;
        if (gy + (int)b->rows  > maxy) maxy = gy + (int)b->rows;
    }

    if (maxx <= minx || maxy <= miny) return false;  /* all whitespace */

    int W = maxx - minx;
    int H = maxy - miny;
    uint32_t *buf = (uint32_t *)calloc((size_t)W * (size_t)H, sizeof(uint32_t));
    if (!buf) return false;

    uint8_t cr = to_u8(layout->color.r);
    uint8_t cg = to_u8(layout->color.g);
    uint8_t cb = to_u8(layout->color.b);

    /* Pass 2: composite each glyph (premultiplied SRC_OVER). */
    for (size_t i = 0; i < layout->count; ++i) {
        if (FT_Load_Glyph(face, layout->glyphs[i].glyph_id,
                          FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
            continue;
        FT_GlyphSlot s = face->glyph;
        FT_Bitmap   *b = &s->bitmap;
        if (b->width == 0 || b->rows == 0) continue;

        int gx = (int)floorf(layout->glyphs[i].x) + s->bitmap_left - minx;
        int gy = (int)floorf(layout->baseline + layout->glyphs[i].y_offset) - s->bitmap_top - miny;

        for (int row = 0; row < (int)b->rows; ++row) {
            int py = gy + row;
            if (py < 0 || py >= H) continue;
            for (int col = 0; col < (int)b->width; ++col) {
                int px = gx + col;
                if (px < 0 || px >= W) continue;
                uint8_t cov = b->buffer[(size_t)row * (size_t)b->pitch + (size_t)col];
                if (!cov) continue;

                uint8_t sa = cov;
                uint8_t sr = (uint8_t)((cr * cov) / 255);
                uint8_t sg = (uint8_t)((cg * cov) / 255);
                uint8_t sb = (uint8_t)((cb * cov) / 255);

                uint32_t *dp = &buf[(size_t)py * (size_t)W + (size_t)px];
                uint32_t d = *dp;
                uint8_t dr = (uint8_t)(d & 0xff);
                uint8_t dg = (uint8_t)((d >> 8) & 0xff);
                uint8_t db = (uint8_t)((d >> 16) & 0xff);
                uint8_t da = (uint8_t)((d >> 24) & 0xff);
                uint8_t inv = (uint8_t)(255 - sa);

                uint8_t orr = (uint8_t)(sr + dr * inv / 255);
                uint8_t org = (uint8_t)(sg + dg * inv / 255);
                uint8_t orb = (uint8_t)(sb + db * inv / 255);
                uint8_t ora = (uint8_t)(sa + da * inv / 255);
                *dp = (uint32_t)orr | ((uint32_t)org << 8) |
                      ((uint32_t)orb << 16) | ((uint32_t)ora << 24);
            }
        }
    }

    flux_image_desc id = {};
    id.type         = FLUX_TYPE_IMAGE_DESC;
    id.width        = (uint32_t)W;
    id.height       = (uint32_t)H;
    id.format       = FLUX_FORMAT_RGBA8_UNORM;
    id.initial_data = buf;

    flux_image *img = NULL;
    flux_result r = flux_image_create(dev, &id, &img);
    free(buf);
    if (r != FLUX_OK || !img) return false;

    layout->image  = img;
    layout->img_w  = W;
    layout->img_h  = H;
    layout->img_dx = (float)minx;
    layout->img_dy = (float)miny;
    return true;
}

bool typio_flux_fill_layout(flux_canvas *canvas, flux_arena *arena,
                            TypioTextLayout *layout, float x, float y)
{
    (void)arena;
    if (!canvas || !layout) return false;

    if (!layout->image_attempted) build_layout_image(layout);
    if (!layout->image) return true;  /* whitespace-only or no device */

    flux_rect dst = {
        x + layout->img_dx, y + layout->img_dy,
        (float)layout->img_w, (float)layout->img_h,
    };
    flux_canvas_draw_image(canvas, layout->image, dst, NULL);
    return true;
}

static TypioTextEngineVTable flux_engine_vtable = {
    .create_layout = flux_create_layout,
    .get_metrics   = flux_get_metrics,
    .get_baseline  = flux_get_baseline,
    .free_layout   = typio_flux_layout_free,
};

TypioTextEngine *typio_flux_engine_create(void)
{
    TypioTextEngine *engine = (TypioTextEngine *)calloc(1, sizeof(*engine));
    if (!engine) return NULL;

    /* The text engine only needs FreeType (shaping + outlines). The Vulkan
     * device is created lazily by the popup when it first presents. */
    if (!ensure_ft_library()) {
        free(engine);
        return NULL;
    }

    engine->priv   = NULL;
    engine->vtable = &flux_engine_vtable;
    return engine;
}

void typio_flux_engine_destroy(TypioTextEngine *engine)
{
    if (!engine) return;
    font_obj_cache_clear();
    font_file_cache_clear();
    free(engine);
}
