#include "internal.h"
#include <math.h>
#include <freetype/ftmm.h>
#include <freetype/fttypes.h>

#define ATLAS_SIZE 2048

static bool ensure_atlas_image(fx_context *ctx)
{
    if (ctx->atlas.image) return true;

    fx_image_desc desc = {
        .width = ATLAS_SIZE,
        .height = ATLAS_SIZE,
        .format = FX_FMT_A8_UNORM,
    };
    ctx->atlas.image = fx_image_create(ctx, &desc);
    if (!ctx->atlas.image) return false;

    ctx->atlas.shelf_y = 2;
    ctx->atlas.shelf_x = 2;
    ctx->atlas.shelf_h = 0;
    return true;
}

static fx_atlas_entry *find_atlas_entry(fx_context *ctx, fx_font *font, uint32_t glyph_id)
{
    for (size_t i = 0; i < ctx->atlas.entry_count; ++i) {
        if (ctx->atlas.entries[i].glyph_id == glyph_id &&
            ctx->atlas.entries[i].font_id == font) {
            return &ctx->atlas.entries[i];
        }
    }
    return NULL;
}

static void atlas_reset(fx_context *ctx)
{
    ctx->atlas.entry_count = 0;
    ctx->atlas.shelf_x = 2;
    ctx->atlas.shelf_y = 2;
    ctx->atlas.shelf_h = 0;
}

static bool upload_glyph(fx_context *ctx, fx_font *font, uint32_t glyph_id, fx_atlas_entry *out_entry)
{
    if (FT_Load_Glyph(font->ft_face, glyph_id, FT_LOAD_RENDER) != 0) return false;

    FT_Bitmap *bm = &font->ft_face->glyph->bitmap;
    int w = (int)bm->width;
    int h = (int)bm->rows;

    if (w + 2 > ATLAS_SIZE || h + 2 > ATLAS_SIZE) {
        FX_LOGE(ctx, "glyph %u is larger than atlas (%dx%d, atlas %d)",
                glyph_id, w, h, ATLAS_SIZE);
        return false;
    }

    if (ctx->atlas.shelf_x + w + 2 > ATLAS_SIZE) {
        ctx->atlas.shelf_x = 2;
        ctx->atlas.shelf_y += ctx->atlas.shelf_h + 2;
        ctx->atlas.shelf_h = 0;
    }

    if (ctx->atlas.shelf_y + h + 2 > ATLAS_SIZE) {
        /* Atlas is full. Evict every cached glyph and reuse the texture.
         * Callers re-ask via fx_atlas_ensure_glyph each frame, so no pointers
         * go stale. We log once per eviction event so runaway glyph churn is
         * visible without flooding the log. */
        FX_LOGW(ctx, "glyph atlas full: evicting %zu entries",
                ctx->atlas.entry_count);
        atlas_reset(ctx);
        /* Wait for in-flight frames to release the atlas before overwriting. */
        vkDeviceWaitIdle(ctx->device);
    }

    if (h > ctx->atlas.shelf_h) ctx->atlas.shelf_h = h;

    int x = ctx->atlas.shelf_x;
    int y = ctx->atlas.shelf_y;

    if (bm->buffer) {
        if (!fx_upload_image(ctx, ctx->atlas.image->vk_image,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             x, y, (uint32_t)w, (uint32_t)h,
                             bm->buffer, (size_t)bm->pitch, 1)) {
            FX_LOGE(ctx, "glyph upload failed (glyph=%u)", glyph_id);
            return false;
        }
    }

    if (ctx->atlas.entry_count + 1 > ctx->atlas.entry_cap) {
        size_t new_cap = ctx->atlas.entry_cap ? ctx->atlas.entry_cap * 2 : 256;
        fx_atlas_entry *new_entries = realloc(ctx->atlas.entries, new_cap * sizeof(fx_atlas_entry));
        if (!new_entries) return false;
        ctx->atlas.entries = new_entries;
        ctx->atlas.entry_cap = new_cap;
    }

    fx_atlas_entry *e = &ctx->atlas.entries[ctx->atlas.entry_count++];
    e->glyph_id = glyph_id;
    e->font_id = font;
    e->w = w;
    e->h = h;
    e->u0 = (float)x / (float)ATLAS_SIZE;
    e->v0 = (float)y / (float)ATLAS_SIZE;
    e->u1 = (float)(x + w) / (float)ATLAS_SIZE;
    e->v1 = (float)(y + h) / (float)ATLAS_SIZE;
    e->bearing_x = font->ft_face->glyph->bitmap_left;
    e->bearing_y = font->ft_face->glyph->bitmap_top;
    e->advance   = (int)font->ft_face->glyph->advance.x >> 6;

    *out_entry = *e;
    ctx->atlas.shelf_x += w + 2;
    return true;
}

static char *dup_nullable_string(const char *src)
{
    size_t len;
    char *copy;
    if (!src) return NULL;
    len = strlen(src);
    copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, src, len + 1);
    return copy;
}

static bool ensure_glyph_capacity(fx_glyph_run *run, size_t extra)
{
    size_t need = run->count + extra;
    if (need <= run->cap) return true;
    size_t new_cap = run->cap ? run->cap : 16;
    while (new_cap < need) new_cap *= 2;
    fx_glyph *glyphs = realloc(run->glyphs, new_cap * sizeof(*glyphs));
    if (!glyphs) return false;
    run->glyphs = glyphs;
    run->cap = new_cap;
    return true;
}

fx_font *fx_font_create(fx_context *ctx, const fx_font_desc *desc)
{
    fx_font *font;
    if (!ctx || !desc || desc->size <= 0.0f || !desc->source_name) return NULL;
    font = calloc(1, sizeof(*font));
    if (!font) return NULL;
    font->ctx = ctx;
    font->family = dup_nullable_string(desc->family);
    font->source_name = dup_nullable_string(desc->source_name);
    if (FT_New_Face(ctx->ft_lib, font->source_name, 0, &font->ft_face) != 0) {
        fx_font_destroy(font);
        return NULL;
    }
    FT_Set_Pixel_Sizes(font->ft_face, 0, (FT_UInt)desc->size);

    /* Apply weight for variable fonts */
    if (desc->weight != 400 && FT_HAS_MULTIPLE_MASTERS(font->ft_face)) {
        FT_MM_Var *mm_var = NULL;
        if (FT_Get_MM_Var(font->ft_face, &mm_var) == 0 && mm_var) {
            for (FT_UInt i = 0; i < mm_var->num_axis; ++i) {
                if (mm_var->axis[i].tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
                    FT_Fixed coords[1];
                    coords[0] = (FT_Fixed)(desc->weight * 65536);
                    FT_Set_Var_Design_Coordinates(font->ft_face, 1, coords);
                    break;
                }
            }
            FT_Done_MM_Var(font->ctx->ft_lib, mm_var);
        }
    }

    font->hb_font = hb_ft_font_create(font->ft_face, NULL);
    font->size = desc->size;
    font->ascender  = (float)(font->ft_face->size->metrics.ascender  >> 6);
    font->descender = (float)(font->ft_face->size->metrics.descender >> 6);
    return font;
}

void fx_font_destroy(fx_font *font)
{
    if (!font) return;
    if (font->hb_font) hb_font_destroy(font->hb_font);
    if (font->ft_face) FT_Done_Face(font->ft_face);
    free(font->family);
    free(font->source_name);
    free(font);
}

bool fx_font_get_desc(const fx_font *font, fx_font_desc *out_desc)
{
    if (!font) return false;
    if (out_desc) {
        out_desc->family = font->family;
        out_desc->source_name = font->source_name;
        out_desc->size = font->size;
    }
    return true;
}

struct hb_font_t *fx_font_get_hb_font(fx_font *font)
{
    return font ? font->hb_font : NULL;
}

void fx_font_get_metrics(const fx_font *font, float *out_ascender,
                         float *out_descender)
{
    if (!font) return;
    if (out_ascender)  *out_ascender  = font->ascender;
    if (out_descender) *out_descender = font->descender;
}

fx_glyph_run *fx_glyph_run_create(size_t reserve_glyphs)
{
    fx_glyph_run *run = calloc(1, sizeof(*run));
    if (!run) return NULL;
    if (reserve_glyphs && !ensure_glyph_capacity(run, reserve_glyphs)) {
        free(run);
        return NULL;
    }
    return run;
}

void fx_glyph_run_destroy(fx_glyph_run *run)
{
    if (!run) return;
    free(run->glyphs);
    free(run);
}

void fx_glyph_run_reset(fx_glyph_run *run)
{
    if (!run) return;
    run->count = 0;
}

bool fx_glyph_run_append(fx_glyph_run *run, uint32_t glyph_id, float x, float y)
{
    if (!run) return false;
    if (!ensure_glyph_capacity(run, 1)) return false;
    run->glyphs[run->count++] = (fx_glyph){ .glyph_id = glyph_id, .x = x, .y = y };
    return true;
}

size_t fx_glyph_run_count(const fx_glyph_run *run)
{
    return run ? run->count : 0;
}

const fx_glyph *fx_glyph_run_data(const fx_glyph_run *run)
{
    return run ? run->glyphs : NULL;
}

/* Internal helper for drawing glyphs */
bool fx_atlas_ensure_glyph(fx_context *ctx, fx_font *font, uint32_t glyph_id, fx_atlas_entry *out_entry)
{
    if (!ensure_atlas_image(ctx)) return false;
    fx_atlas_entry *e = find_atlas_entry(ctx, font, glyph_id);
    if (e) {
        *out_entry = *e;
        return true;
    }
    return upload_glyph(ctx, font, glyph_id, out_entry);
}
