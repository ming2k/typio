#include "internal.h"
#include <math.h>

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

static bool upload_glyph(fx_context *ctx, fx_font *font, uint32_t glyph_id, fx_atlas_entry *out_entry)
{
    if (FT_Load_Glyph(font->ft_face, glyph_id, FT_LOAD_RENDER) != 0) return false;

    FT_Bitmap *bm = &font->ft_face->glyph->bitmap;
    int w = (int)bm->width;
    int h = (int)bm->rows;

    if (ctx->atlas.shelf_x + w + 2 > ATLAS_SIZE) {
        ctx->atlas.shelf_x = 2;
        ctx->atlas.shelf_y += ctx->atlas.shelf_h + 2;
        ctx->atlas.shelf_h = 0;
    }

    if (ctx->atlas.shelf_y + h + 2 > ATLAS_SIZE) {
        /* Atlas full! In a production impl we'd clear or grow.
         * For now, we'll just fail. */
        return false;
    }

    if (h > ctx->atlas.shelf_h) ctx->atlas.shelf_h = h;

    int x = ctx->atlas.shelf_x;
    int y = ctx->atlas.shelf_y;

    /* Update GPU texture */
    if (bm->buffer) {
        /* We need a specialized partial upload for the atlas.
         * For now, I'll use a slow path: recreate staging and copy.
         * A better way is a persistent staging buffer. */
        
        VkBuffer staging;
        VkDeviceMemory staging_mem;
        VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = (size_t)w * h, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT };
        vkCreateBuffer(ctx->device, &bci, NULL, &staging);
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(ctx->device, staging, &mr);
        VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size };
        for (uint32_t i = 0; i < ctx->mem_props.memoryTypeCount; ++i)
            if ((mr.memoryTypeBits & (1 << i)) && (ctx->mem_props.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))
                { mai.memoryTypeIndex = i; break; }
        vkAllocateMemory(ctx->device, &mai, NULL, &staging_mem);
        vkBindBufferMemory(ctx->device, staging, staging_mem, 0);

        void *mapped;
        vkMapMemory(ctx->device, staging_mem, 0, (size_t)w * h, 0, &mapped);
        if (bm->pitch == w) {
            memcpy(mapped, bm->buffer, (size_t)w * h);
        } else {
            for (int i = 0; i < h; ++i)
                memcpy((uint8_t *)mapped + i * w, bm->buffer + i * bm->pitch, (size_t)w);
        }
        vkUnmapMemory(ctx->device, staging_mem);

        VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = ctx->frame_cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
        VkCommandBuffer cb;
        vkAllocateCommandBuffers(ctx->device, &cai, &cb);
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        vkBeginCommandBuffer(cb, &bi);

        VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .image = ctx->atlas.image->vk_image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

        VkBufferImageCopy region = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageOffset = { (int32_t)x, (int32_t)y, 0 }, .imageExtent = { (uint32_t)w, (uint32_t)h, 1 } };
        vkCmdCopyBufferToImage(cb, staging, ctx->atlas.image->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

        vkEndCommandBuffer(cb);
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
        vkQueueSubmit(ctx->graphics_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(ctx->graphics_queue);

        vkFreeCommandBuffers(ctx->device, ctx->frame_cmd_pool, 1, &cb);
        vkDestroyBuffer(ctx->device, staging, NULL);
        vkFreeMemory(ctx->device, staging_mem, NULL);
    }

    /* Record entry */
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
