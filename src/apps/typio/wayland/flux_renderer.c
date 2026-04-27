#include "flux_renderer.h"

#include <fontconfig/fontconfig.h>
#include <harfbuzz/hb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct TypioTextLayout {
    fx_font      *font;
    fx_glyph_run *run;
    fx_paint      paint;
    float         width;
    float         height;
    float         baseline;
};

typedef struct {
    fx_context *ctx;
} FluxTextEnginePriv;

static fx_context *global_ctx;

/* ── Font file cache ────────────────────────────────────────────────── */
#define FONT_FILE_CACHE_CAP 8

typedef struct {
    char   family[128];
    int32_t weight;
    char  *path;
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

/* ── Font object cache ──────────────────────────────────────────────── */
#define FONT_OBJ_CACHE_CAP 16

typedef struct {
    char  *path;
    float  size;
    fx_font *font;
} FontObjEntry;

static FontObjEntry font_obj_cache[FONT_OBJ_CACHE_CAP];
static size_t       font_obj_cache_count = 0;

static void font_obj_cache_clear(void)
{
    for (size_t i = 0; i < font_obj_cache_count; ++i) {
        fx_font_destroy(font_obj_cache[i].font);
        free(font_obj_cache[i].path);
    }
    font_obj_cache_count = 0;
}

static fx_font *font_obj_cache_lookup(const char *path, float size)
{
    for (size_t i = 0; i < font_obj_cache_count; ++i) {
        if (font_obj_cache[i].size == size &&
            strcmp(font_obj_cache[i].path, path) == 0) {
            return font_obj_cache[i].font;
        }
    }
    return NULL;
}

static void font_obj_cache_insert(const char *path, float size, fx_font *font)
{
    if (font_obj_cache_count < FONT_OBJ_CACHE_CAP) {
        FontObjEntry *e = &font_obj_cache[font_obj_cache_count++];
        e->path = strdup(path);
        e->size = size;
        e->font = font;
    } else {
        free(font_obj_cache[0].path);
        for (size_t i = 1; i < FONT_OBJ_CACHE_CAP; ++i)
            font_obj_cache[i - 1] = font_obj_cache[i];
        FontObjEntry *e = &font_obj_cache[FONT_OBJ_CACHE_CAP - 1];
        e->path = strdup(path);
        e->size = size;
        e->font = font;
    }
}

static fx_font *get_or_create_font(fx_context *ctx, const char *path, float size, int32_t weight)
{
    fx_font *font = font_obj_cache_lookup(path, size);
    if (font) return font;

    fx_font_desc desc = {
        .family = "",
        .source_name = path,
        .size = size,
        .weight = weight,
        .italic = false,
    };
    font = fx_font_create(ctx, &desc);
    if (font) {
        font_obj_cache_insert(path, size, font);
    }
    return font;
}

/* ── Helpers ────────────────────────────────────────────────────────── */

static unsigned char to_u8(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (unsigned char)(v * 255.0f + 0.5f);
}

fx_color typio_flux_color(TypioColor color)
{
    return fx_color_rgba(to_u8(color.r), to_u8(color.g), to_u8(color.b), to_u8(color.a));
}

static void flux_log(fx_log_level level, const char *msg, void *user)
{
    (void)user;
    (void)level;
    (void)msg;
}

fx_context *typio_flux_context_get(void)
{
    if (global_ctx) return global_ctx;

    fx_context_desc desc = {
        .log = flux_log,
        .log_user = NULL,
        .enable_validation = false,
        .app_name = "typio",
    };
    global_ctx = fx_context_create(&desc);
    return global_ctx;
}

static int32_t parse_weight_keyword(const char *s, size_t len)
{
    if (len == 6 && strncasecmp(s, "Medium", 6) == 0)  return 500;
    if (len == 4 && strncasecmp(s, "Bold", 4) == 0)    return 700;
    if (len == 6 && strncasecmp(s, "Normal", 6) == 0)  return 400;
    if (len == 7 && strncasecmp(s, "Regular", 7) == 0) return 400;
    if (len == 5 && strncasecmp(s, "Light", 5) == 0)   return 300;
    if (len == 4 && strncasecmp(s, "Thin", 4) == 0)    return 100;
    if (len == 9 && strncasecmp(s, "ExtraBold", 9) == 0) return 800;
    if (len == 5 && strncasecmp(s, "Black", 5) == 0)   return 900;
    if (len == 8 && strncasecmp(s, "SemiBold", 8) == 0) return 600;
    if (len == 10 && strncasecmp(s, "ExtraLight", 10) == 0) return 200;
    {
        /* numeric weight like "500" */
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
    /* Map CSS weight (100-900) to fontconfig weight */
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

static char *find_fallback_font(const char *text, int32_t weight)
{
    if (!text || !text[0]) return NULL;
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
    FcPatternAddCharSet(pat, FC_CHARSET, cs);
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
    FcCharSetDestroy(cs);
    FcPatternDestroy(pat);
    return result;
}

static bool layout_has_missing_glyphs(const TypioTextLayout *layout)
{
    if (!layout || !layout->run) return false;
    size_t count = fx_glyph_run_count(layout->run);
    const fx_glyph *glyphs = fx_glyph_run_data(layout->run);
    for (size_t i = 0; i < count; ++i) {
        if (glyphs[i].glyph_id == 0) return true;
    }
    return false;
}

static void flux_free_layout_internal(TypioTextLayout *layout)
{
    if (!layout) return;
    fx_glyph_run_destroy(layout->run);
    free(layout);
}

static TypioTextLayout *flux_shape_text(fx_context *ctx,
                                        const char *text,
                                        fx_font *font,
                                        TypioColor color)
{
    TypioTextLayout *layout = (TypioTextLayout *)calloc(1, sizeof(*layout));
    if (!layout) return NULL;

    layout->font = font;
    layout->run = fx_glyph_run_create(text ? strlen(text) : 0);
    if (!layout->run) goto fail;

    fx_paint_init(&layout->paint, typio_flux_color(color));
    fx_font_get_metrics(layout->font, &layout->baseline, NULL);
    {
        float descender = 0.0f;
        fx_font_get_metrics(layout->font, NULL, &descender);
        layout->height = layout->baseline - descender;
    }

    hb_buffer_t *hb = hb_buffer_create();
    if (!hb) goto fail;
    hb_buffer_add_utf8(hb, text ? text : "", -1, 0, -1);
    hb_buffer_guess_segment_properties(hb);
    hb_shape(fx_font_get_hb_font(layout->font), hb, NULL, 0);

    hb_glyph_info_t *infos;
    hb_glyph_position_t *positions;
    unsigned int count = 0;
    infos = hb_buffer_get_glyph_infos(hb, &count);
    positions = hb_buffer_get_glyph_positions(hb, &count);
    float pen_x = 0.0f;
    for (unsigned int i = 0; i < count; ++i) {
        float x_offset = (float)positions[i].x_offset / 64.0f;
        float y_offset = -(float)positions[i].y_offset / 64.0f;
        if (!fx_glyph_run_append(layout->run, infos[i].codepoint,
                                 pen_x + x_offset, y_offset)) {
            hb_buffer_destroy(hb);
            goto fail;
        }
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
    FluxTextEnginePriv *priv = (FluxTextEnginePriv *)((TypioTextEngine *)engine)->priv;
    char family[128];
    char *font_file = NULL;
    char *fb_file = NULL;
    float size_px;
    fx_font *font = NULL;
    TypioTextLayout *layout = NULL;
    TypioTextLayout *fb_layout = NULL;
    int32_t weight = 400;

    if (!priv || !priv->ctx) return NULL;
    if (!parse_font_desc(font_desc, family, sizeof(family), &size_px, &weight)) return NULL;

    font_file = match_font_file(family, weight);
    if (!font_file) return NULL;

    font = get_or_create_font(priv->ctx, font_file, size_px, weight);
    if (!font) goto fail;

    layout = flux_shape_text(priv->ctx, text, font, color);
    if (!layout) goto fail;

    if (layout_has_missing_glyphs(layout)) {
        fb_file = find_fallback_font(text, weight);
        if (fb_file && strcmp(fb_file, font_file) != 0) {
            fx_font *fb_font = get_or_create_font(priv->ctx, fb_file, size_px, weight);
            if (fb_font) {
                fb_layout = flux_shape_text(priv->ctx, text, fb_font, color);
                if (fb_layout && !layout_has_missing_glyphs(fb_layout)) {
                    flux_free_layout_internal(layout);
                    layout = fb_layout;
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
    if (out_w) *out_w = layout ? layout->width : 0.0f;
    if (out_h) *out_h = layout ? layout->height : 0.0f;
}

static float flux_get_baseline(TypioTextLayout *layout)
{
    return layout ? layout->baseline : 0.0f;
}

void typio_flux_layout_free(TypioTextLayout *layout)
{
    if (!layout) return;
    fx_glyph_run_destroy(layout->run);
    /* font is owned by global cache; do not destroy here */
    free(layout);
}

bool typio_flux_draw_layout(fx_canvas *canvas,
                            TypioTextLayout *layout,
                            float x,
                            float y)
{
    if (!canvas || !layout || !layout->font || !layout->run) return false;
    return fx_draw_glyph_run(canvas, layout->font, layout->run,
                             x, y + layout->baseline, &layout->paint);
}

static TypioTextEngineVTable flux_engine_vtable = {
    .create_layout = flux_create_layout,
    .get_metrics = flux_get_metrics,
    .get_baseline = flux_get_baseline,
    .free_layout = typio_flux_layout_free,
};

TypioTextEngine *typio_flux_engine_create(void)
{
    TypioTextEngine *engine;
    FluxTextEnginePriv *priv;

    engine = (TypioTextEngine *)calloc(1, sizeof(*engine));
    priv = (FluxTextEnginePriv *)calloc(1, sizeof(*priv));
    if (!engine || !priv) {
        free(engine);
        free(priv);
        return NULL;
    }

    priv->ctx = typio_flux_context_get();
    if (!priv->ctx) {
        free(priv);
        free(engine);
        return NULL;
    }

    engine->priv = priv;
    engine->vtable = &flux_engine_vtable;
    return engine;
}

void typio_flux_engine_destroy(TypioTextEngine *engine)
{
    if (!engine) return;
    font_obj_cache_clear();
    font_file_cache_clear();
    free(engine->priv);
    free(engine);
}
