#include "flux/flux_wayland.h"
#include "flux/flux_vulkan.h"
#include "internal.h"
#include "vk/vk_mem_alloc.h"

#include <math.h>

#include <vulkan/vulkan_wayland.h>

/* Converts 0xAARRGGBB premultiplied to a VkClearColorValue.
 * When targetting a SRGB swapchain format, Vulkan expects linear
 * floats — the presentation hardware applies the sRGB EOTF. We treat
 * the input color as sRGB 8-bit and convert to linear here. */
static VkClearColorValue to_clear_color(fx_color c, VkFormat fmt)
{
    float a = ((c >> 24) & 0xFF) / 255.0f;
    float r = ((c >> 16) & 0xFF) / 255.0f;
    float g = ((c >>  8) & 0xFF) / 255.0f;
    float b = ((c      ) & 0xFF) / 255.0f;

    bool srgb = (fmt == VK_FORMAT_B8G8R8A8_SRGB ||
                 fmt == VK_FORMAT_R8G8B8A8_SRGB);
    if (srgb) {
#       define EOTF(v) ((v) <= 0.04045f \
                        ? (v) / 12.92f \
                        : powf(((v) + 0.055f) / 1.055f, 2.4f))
        r = EOTF(r);
        g = EOTF(g);
        b = EOTF(b);
#       undef EOTF
    }
    VkClearColorValue out;
    out.float32[0] = r;
    out.float32[1] = g;
    out.float32[2] = b;
    out.float32[3] = a;
    return out;
}

static bool rect_has_area(const fx_rect *rect)
{
    return rect && rect->w > 0.0f && rect->h > 0.0f;
}

static void bind_solid_pipeline(fx_surface *s, VkCommandBuffer cmd)
{
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)s->extent.width,
        .height = (float)s->extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      s->pc->solid_rect_pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
}

static void set_viewport(fx_surface *s, VkCommandBuffer cmd)
{
    VkViewport viewport = {
        .x = 0.0f, .y = 0.0f,
        .width = (float)s->extent.width,
        .height = (float)s->extent.height,
        .minDepth = 0.0f, .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
}

static void push_solid_color(fx_surface *s, VkCommandBuffer cmd, fx_color color)
{
    VkClearColorValue linear = to_clear_color(color, s->surface_format.format);
    fx_solid_color_pc pc = {
        .surface_size = { (float)s->extent.width, (float)s->extent.height },
        .mode = 0, .pad = 0.0f,
        .color = {
            linear.float32[0],
            linear.float32[1],
            linear.float32[2],
            linear.float32[3],
        },
    };

    vkCmdPushConstants(cmd, s->pc->solid_rect_layout,
                       VK_SHADER_STAGE_VERTEX_BIT |
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
}

typedef struct {
    fx_color color;
    VkBuffer vbuf;
    uint32_t first_vertex;
    uint32_t vertex_count;
    bool active;
} fx_batch;

static void flush_batch(fx_surface *s, VkCommandBuffer cmd, fx_batch *batch)
{
    if (!batch->active || batch->vertex_count == 0) return;

    push_solid_color(s, cmd, batch->color);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &batch->vbuf, &offset);
    vkCmdDraw(cmd, batch->vertex_count, 1, batch->first_vertex, 0);

    batch->vertex_count = 0;
    batch->active = false;
}

static bool add_to_batch(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                         fx_batch *batch, fx_color color,
                         const fx_solid_vertex *verts, size_t count)
{
    if (batch->active && (batch->color != color)) {
        flush_batch(s, cmd, batch);
    }

    VkBuffer vbuf;
    VkDeviceSize offset;
    fx_solid_vertex *map = fx_vbuf_pool_alloc(&fr->vbuf, count * sizeof(fx_solid_vertex), &vbuf, &offset);
    if (!map) return false;

    uint32_t first = (uint32_t)(offset / sizeof(fx_solid_vertex));

    if (batch->active && batch->vbuf == vbuf && (batch->first_vertex + batch->vertex_count == first)) {
        /* Continue existing batch */
        memcpy(map, verts, count * sizeof(fx_solid_vertex));
        batch->vertex_count += (uint32_t)count;
    } else {
        /* Start new batch */
        flush_batch(s, cmd, batch);
        memcpy(map, verts, count * sizeof(fx_solid_vertex));
        batch->active = true;
        batch->color = color;
        batch->vbuf = vbuf;
        batch->first_vertex = first;
        batch->vertex_count = (uint32_t)count;
    }
    return true;
}

static bool draw_solid_rect(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                            fx_batch *batch, const fx_rect *rect, fx_color color)
{
    fx_solid_vertex verts[6];

    if (!rect_has_area(rect)) return false;

    verts[0] = (fx_solid_vertex){ .pos = { rect->x, rect->y } };
    verts[1] = (fx_solid_vertex){ .pos = { rect->x + rect->w, rect->y } };
    verts[2] = (fx_solid_vertex){ .pos = { rect->x + rect->w, rect->y + rect->h } };
    verts[3] = (fx_solid_vertex){ .pos = { rect->x, rect->y } };
    verts[4] = (fx_solid_vertex){ .pos = { rect->x + rect->w, rect->y + rect->h } };
    verts[5] = (fx_solid_vertex){ .pos = { rect->x, rect->y + rect->h } };

    return add_to_batch(s, fr, cmd, batch, color, verts, 6);
}

static bool draw_polygon_fill(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                              fx_batch *batch, const fx_point *points, size_t count,
                              fx_color color)
{
    fx_point *tris = NULL;
    size_t tri_points = 0;
    fx_solid_vertex *verts = NULL;

    if (!points || count < 3) return false;

    if (!fx_tessellate_simple_polygon(points, count, &fr->arena, &tris, &tri_points))
        return false;

    /* vertices can be allocated in the arena too */
    verts = fx_arena_alloc(&fr->arena, tri_points * sizeof(*verts));
    if (!verts) return false;

    for (size_t i = 0; i < tri_points; ++i)
        verts[i] = (fx_solid_vertex){ .pos = { tris[i].x, tris[i].y } };

    return add_to_batch(s, fr, cmd, batch, color, verts, tri_points);
}

static bool draw_rect_stroke(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                             fx_batch *batch, const fx_rect *rect, float width,
                             fx_color color)
{
    float half_w;
    fx_rect outer;
    fx_rect inner;
    bool emitted = false;

    if (!rect || width <= 0.0f) return false;

    half_w = width * 0.5f;
    outer = (fx_rect){
        .x = rect->x - half_w,
        .y = rect->y - half_w,
        .w = rect->w + width,
        .h = rect->h + width,
    };
    inner = (fx_rect){
        .x = rect->x + half_w,
        .y = rect->y + half_w,
        .w = rect->w - width,
        .h = rect->h - width,
    };

    if (inner.w <= 0.0f || inner.h <= 0.0f)
        return draw_solid_rect(s, fr, cmd, batch, &outer, color);

    emitted |= draw_solid_rect(s, fr, cmd, batch, &(fx_rect){
        .x = outer.x,
        .y = outer.y,
        .w = outer.w,
        .h = inner.y - outer.y,
    }, color);
    emitted |= draw_solid_rect(s, fr, cmd, batch, &(fx_rect){
        .x = outer.x,
        .y = inner.y + inner.h,
        .w = outer.w,
        .h = (outer.y + outer.h) - (inner.y + inner.h),
    }, color);
    emitted |= draw_solid_rect(s, fr, cmd, batch, &(fx_rect){
        .x = outer.x,
        .y = inner.y,
        .w = inner.x - outer.x,
        .h = inner.h,
    }, color);
    emitted |= draw_solid_rect(s, fr, cmd, batch, &(fx_rect){
        .x = inner.x + inner.w,
        .y = inner.y,
        .w = (outer.x + outer.w) - (inner.x + inner.w),
        .h = inner.h,
    }, color);

    return emitted;
}

static void bind_gradient_pipeline(fx_surface *s, VkCommandBuffer cmd,
                                   const fx_gradient *gradient)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pc->gradient_pipeline);
    set_viewport(s, cmd);

    fx_gradient_pc pc = {
        .surface_size = { (float)s->extent.width, (float)s->extent.height },
        .mode = gradient->mode,
        .stop_count = gradient->stop_count,
        .start = { gradient->start[0], gradient->start[1] },
        .end = { gradient->end[0], gradient->end[1] },
    };
    memcpy(pc.colors, gradient->colors, sizeof(pc.colors));
    memcpy(pc.stops, gradient->stops, sizeof(pc.stops));

    vkCmdPushConstants(cmd, s->pc->gradient_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
}

static bool draw_gradient_tris(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                               fx_batch *batch, const fx_solid_vertex *verts,
                               size_t count, const fx_gradient *gradient)
{
    flush_batch(s, cmd, batch);

    VkBuffer vbuf;
    VkDeviceSize offset;
    void *map = fx_vbuf_pool_alloc(&fr->vbuf, count * sizeof(*verts), &vbuf, &offset);
    if (!map) return false;
    memcpy(map, verts, count * sizeof(*verts));

    bind_gradient_pipeline(s, cmd, gradient);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &offset);
    vkCmdDraw(cmd, (uint32_t)count, 1, 0, 0);

    bind_solid_pipeline(s, cmd);
    return true;
}

static bool draw_gradient_rect(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                               fx_batch *batch, const fx_rect *rect,
                               const fx_gradient *gradient)
{
    fx_solid_vertex verts[6];

    if (!rect_has_area(rect)) return false;

    verts[0] = (fx_solid_vertex){ .pos = { rect->x, rect->y } };
    verts[1] = (fx_solid_vertex){ .pos = { rect->x + rect->w, rect->y } };
    verts[2] = (fx_solid_vertex){ .pos = { rect->x + rect->w, rect->y + rect->h } };
    verts[3] = (fx_solid_vertex){ .pos = { rect->x, rect->y } };
    verts[4] = (fx_solid_vertex){ .pos = { rect->x + rect->w, rect->y + rect->h } };
    verts[5] = (fx_solid_vertex){ .pos = { rect->x, rect->y + rect->h } };

    return draw_gradient_tris(s, fr, cmd, batch, verts, 6, gradient);
}

static bool draw_gradient_polygon_fill(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                                       fx_batch *batch, const fx_point *points,
                                       size_t count, const fx_gradient *gradient)
{
    fx_point *tris = NULL;
    size_t tri_points = 0;
    fx_solid_vertex *verts = NULL;

    if (!points || count < 3) return false;

    if (!fx_tessellate_simple_polygon(points, count, &fr->arena, &tris, &tri_points))
        return false;

    verts = fx_arena_alloc(&fr->arena, tri_points * sizeof(*verts));
    if (!verts) return false;

    for (size_t i = 0; i < tri_points; ++i)
        verts[i] = (fx_solid_vertex){ .pos = { tris[i].x, tris[i].y } };

    return draw_gradient_tris(s, fr, cmd, batch, verts, tri_points, gradient);
}

static VkDescriptorSet get_cached_ds(fx_frame *fr, VkImageView view, VkSampler sampler,
                                       VkDescriptorSetLayout layout, VkDevice device)
{
    for (uint32_t i = 0; i < fr->desc_cache_count; ++i) {
        if (fr->desc_cache[i].image_view == view && fr->desc_cache[i].sampler == sampler)
            return fr->desc_cache[i].ds;
    }
    VkDescriptorSetAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = fr->desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout,
    };
    VkDescriptorSet ds;
    if (vkAllocateDescriptorSets(device, &ai, &ds) != VK_SUCCESS) return VK_NULL_HANDLE;

    VkDescriptorImageInfo dii = {
        .sampler = sampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = ds,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &dii,
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);

    if (fr->desc_cache_count < FX_FRAME_DESC_CACHE_SIZE) {
        fr->desc_cache[fr->desc_cache_count++] = (fx_desc_cache_entry){
            .image_view = view, .sampler = sampler, .ds = ds,
        };
    }
    return ds;
}

static bool draw_image_quad(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                            fx_batch *batch, const fx_image *image,
                            const fx_rect *src, const fx_rect *dst)
{
    flush_batch(s, cmd, batch);

    fx_image_vertex verts[6];
    float sw = (float)image->desc.width;
    float sh = (float)image->desc.height;
    float u0 = src->x / sw, v0 = src->y / sh;
    float u1 = (src->x + src->w) / sw, v1 = (src->y + src->h) / sh;

    verts[0] = (fx_image_vertex){ { dst->x, dst->y }, { u0, v0 } };
    verts[1] = (fx_image_vertex){ { dst->x + dst->w, dst->y }, { u1, v0 } };
    verts[2] = (fx_image_vertex){ { dst->x + dst->w, dst->y + dst->h }, { u1, v1 } };
    verts[3] = (fx_image_vertex){ { dst->x, dst->y }, { u0, v0 } };
    verts[4] = (fx_image_vertex){ { dst->x + dst->w, dst->y + dst->h }, { u1, v1 } };
    verts[5] = (fx_image_vertex){ { dst->x, dst->y + dst->h }, { u0, v1 } };

    VkBuffer vbuf;
    VkDeviceSize offset;
    void *map = fx_vbuf_pool_alloc(&fr->vbuf, sizeof(verts), &vbuf, &offset);
    if (!map) return false;
    memcpy(map, verts, sizeof(verts));

    /* Descriptor Set (cached per frame) */
    VkDescriptorSet ds = get_cached_ds(fr, image->vk_view, s->sampler,
                                       s->pc->image_dsl, s->ctx->device);
    if (ds == VK_NULL_HANDLE) return false;

    /* Track fence for WAR hazard detection */
    ((fx_image *)image)->last_use_fence = fr->in_flight;

    /* Draw */
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pc->image_pipeline);
    set_viewport(s, cmd);
    
    fx_image_pc pc = { .surface_size = { (float)s->extent.width, (float)s->extent.height } };
    vkCmdPushConstants(cmd, s->pc->image_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pc->image_layout, 0, 1, &ds, 0, NULL);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &offset);
    vkCmdDraw(cmd, 6, 1, 0, 0);

    /* Re-bind solid pipeline for subsequent ops */
    bind_solid_pipeline(s, cmd);
    return true;
}

static bool draw_glyph_run(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                           fx_batch *batch, const fx_draw_glyphs_op *op)
{
    flush_batch(s, cmd, batch);

    size_t count = fx_glyph_run_count(op->run);
    const fx_glyph *glyphs = fx_glyph_run_data(op->run);
    
    /* vertices can be allocated in the arena */
    fx_image_vertex *verts = fx_arena_alloc(&fr->arena, count * 6 * sizeof(fx_image_vertex));
    if (!verts) return false;

    size_t active_count = 0;
    for (size_t i = 0; i < count; ++i) {
        fx_atlas_entry ent;
        if (!fx_atlas_ensure_glyph(s->ctx, (fx_font *)op->font, glyphs[i].glyph_id, &ent)) continue;

        float x = op->x + glyphs[i].x + (float)ent.bearing_x;
        float y = op->y + glyphs[i].y - (float)ent.bearing_y;
        float w = (float)ent.w;
        float h = (float)ent.h;

        fx_image_vertex *v = &verts[active_count * 6];
        v[0] = (fx_image_vertex){ { x,     y     }, { ent.u0, ent.v0 } };
        v[1] = (fx_image_vertex){ { x + w, y     }, { ent.u1, ent.v0 } };
        v[2] = (fx_image_vertex){ { x + w, y + h }, { ent.u1, ent.v1 } };
        v[3] = (fx_image_vertex){ { x,     y     }, { ent.u0, ent.v0 } };
        v[4] = (fx_image_vertex){ { x + w, y + h }, { ent.u1, ent.v1 } };
        v[5] = (fx_image_vertex){ { x,     y + h }, { ent.u0, ent.v1 } };
        active_count++;
    }

    if (active_count == 0) return true;

    VkBuffer vbuf;
    VkDeviceSize offset;
    void *map = fx_vbuf_pool_alloc(&fr->vbuf, active_count * 6 * sizeof(fx_image_vertex), &vbuf, &offset);
    if (!map) return false;
    memcpy(map, verts, active_count * 6 * sizeof(fx_image_vertex));

    /* Descriptor Set for Atlas (cached per frame) */
    VkDescriptorSet ds = get_cached_ds(fr, s->ctx->atlas.image->vk_view, s->sampler,
                                       s->pc->image_dsl, s->ctx->device);
    if (ds == VK_NULL_HANDLE) return false;

    /* Draw */
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pc->text_pipeline);
    set_viewport(s, cmd);
    
    VkClearColorValue linear = to_clear_color(op->paint.color, s->surface_format.format);
    fx_text_pc pc = {
        .surface_size = { (float)s->extent.width, (float)s->extent.height },
        .color = { linear.float32[0], linear.float32[1], linear.float32[2], linear.float32[3] },
    };
    vkCmdPushConstants(cmd, s->pc->text_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pc->text_layout, 0, 1, &ds, 0, NULL);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &offset);
    vkCmdDraw(cmd, (uint32_t)active_count * 6, 1, 0, 0);

    /* Re-bind solid pipeline */
    bind_solid_pipeline(s, cmd);
    return true;
}

static void draw_clip_path_stencil(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                                   const fx_path *cpath)
{
    const fx_point *cp = NULL;
    fx_point *cp_flat = NULL;
    size_t cc = 0;
    bool got_points = false;
    if (fx_path_get_line_loop(cpath, &cp, &cc)) {
        got_points = true;
    } else if (fx_path_flatten_line_loop(cpath, 0.25f, &fr->arena, &cp_flat, &cc)) {
        cp = cp_flat;
        got_points = true;
    }
    if (got_points && cc >= 3) {
        fx_point *tris = NULL;
        size_t tri_count = 0;
        if (fx_tessellate_simple_polygon(cp, cc, &fr->arena, &tris, &tri_count)) {
            fx_solid_vertex *sv = fx_arena_alloc(&fr->arena, tri_count * sizeof(*sv));
            if (sv) {
                for (size_t i = 0; i < tri_count; ++i)
                    sv[i] = (fx_solid_vertex){ .pos = { tris[i].x, tris[i].y } };

                VkBuffer vbuf;
                VkDeviceSize offset;
                void *map = fx_vbuf_pool_alloc(&fr->vbuf, tri_count * sizeof(*sv), &vbuf, &offset);
                if (map) {
                    memcpy(map, sv, tri_count * sizeof(*sv));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pc->stencil_pipeline);
                    set_viewport(s, cmd);
                    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 1);
                    fx_solid_color_pc pc = {
                        .surface_size = { (float)s->extent.width, (float)s->extent.height },
                    };
                    vkCmdPushConstants(cmd, s->pc->stencil_layout,
                                       VK_SHADER_STAGE_VERTEX_BIT,
                                       0, sizeof(pc), &pc);
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &offset);
                    vkCmdDraw(cmd, (uint32_t)tri_count, 1, 0, 0);
                }
            }
        }
    }
}

static size_t record_ops(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd)
{
    size_t executed = 0;
    fx_batch batch = { .active = false };
    VkRect2D full_scissor = { .offset = {0, 0}, .extent = s->extent };
    VkRect2D current_scissor = full_scissor;
    uint32_t current_stencil_ref = 0;
    const fx_path *active_clip_path = NULL;

    bind_solid_pipeline(s, cmd);
    vkCmdSetScissor(cmd, 0, 1, &current_scissor);
    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, current_stencil_ref);

    for (size_t i = 0; i < s->canvas.op_count; ++i) {
        const fx_op *op = &s->canvas.ops[i];

        if (op->kind == FX_OP_CLIP_RECT) {
            flush_batch(s, cmd, &batch);
            fx_rect r = op->u.clip_rect.rect;
            int32_t x = (int32_t)roundf(r.x);
            int32_t y = (int32_t)roundf(r.y);
            int32_t w = (int32_t)roundf(r.w);
            int32_t h = (int32_t)roundf(r.h);
            if (x < 0) { w += x; x = 0; }
            if (y < 0) { h += y; y = 0; }
            if (x + w > (int32_t)s->extent.width)  w = (int32_t)s->extent.width - x;
            if (y + h > (int32_t)s->extent.height) h = (int32_t)s->extent.height - y;
            if (w < 0) w = 0;
            if (h < 0) h = 0;
            current_scissor.offset.x = x;
            current_scissor.offset.y = y;
            current_scissor.extent.width  = (uint32_t)w;
            current_scissor.extent.height = (uint32_t)h;
            vkCmdSetScissor(cmd, 0, 1, &current_scissor);
            continue;
        } else if (op->kind == FX_OP_RESET_CLIP) {
            flush_batch(s, cmd, &batch);
            current_scissor = full_scissor;
            vkCmdSetScissor(cmd, 0, 1, &current_scissor);
            VkClearAttachment clear = {
                .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
                .clearValue = { .depthStencil = { .stencil = 0 } },
            };
            VkClearRect rect = {
                .rect = full_scissor,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            vkCmdClearAttachments(cmd, 1, &clear, 1, &rect);
            active_clip_path = NULL;
            current_stencil_ref = 0;
            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, current_stencil_ref);
            continue;
        } else if (op->kind == FX_OP_CLIP_PATH) {
            flush_batch(s, cmd, &batch);
            fx_rect r;
            if (fx_path_get_bounds(op->u.clip_path.path, &r)) {
                int32_t x = (int32_t)roundf(r.x);
                int32_t y = (int32_t)roundf(r.y);
                int32_t w = (int32_t)roundf(r.w);
                int32_t h = (int32_t)roundf(r.h);
                if (x < 0) { w += x; x = 0; }
                if (y < 0) { h += y; y = 0; }
                if (x + w > (int32_t)s->extent.width)  w = (int32_t)s->extent.width - x;
                if (y + h > (int32_t)s->extent.height) h = (int32_t)s->extent.height - y;
                if (w < 0) w = 0;
                if (h < 0) h = 0;
                current_scissor.offset.x = x;
                current_scissor.offset.y = y;
                current_scissor.extent.width  = (uint32_t)w;
                current_scissor.extent.height = (uint32_t)h;
                vkCmdSetScissor(cmd, 0, 1, &current_scissor);

                /* Clear stencil to 0 inside the clip bbox */
                VkClearAttachment clear = {
                    .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
                    .clearValue = { .depthStencil = { .stencil = 0 } },
                };
                VkClearRect crect = {
                    .rect = current_scissor,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                };
                vkCmdClearAttachments(cmd, 1, &clear, 1, &crect);
            }

            draw_clip_path_stencil(s, fr, cmd, op->u.clip_path.path);
            active_clip_path = op->u.clip_path.path;
            bind_solid_pipeline(s, cmd);
            current_stencil_ref = 1;
            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, current_stencil_ref);
            continue;
        }

        fx_rect rect;
        const fx_point *points = NULL;
        size_t point_count = 0;
        fx_point *flattened = NULL;
        bool closed = false;
        fx_point *stroke_tris = NULL;
        size_t stroke_count = 0;

        if (op->kind == FX_OP_FILL_RECT) {
            if (draw_solid_rect(s, fr, cmd, &batch, &op->u.fill_rect.rect, op->u.fill_rect.color))
                executed++;
            continue;
        } else if (op->kind == FX_OP_FILL_PATH) {
            const fx_gradient *gradient = op->u.fill_path.paint.gradient;
            const fx_path *fpath = op->u.fill_path.path;
            if (fx_path_is_axis_aligned_rect(fpath, &rect)) {
                if (gradient) {
                    if (draw_gradient_rect(s, fr, cmd, &batch, &rect, gradient))
                        executed++;
                } else {
                    if (draw_solid_rect(s, fr, cmd, &batch, &rect, op->u.fill_path.paint.color))
                        executed++;
                }
                continue;
            }
            size_t sub_count = fx_path_subpath_count(fpath);
            if (sub_count == 1 &&
                fx_path_get_line_loop(fpath, &points, &point_count)) {
                if (gradient) {
                    if (draw_gradient_polygon_fill(s, fr, cmd, &batch, points, point_count, gradient))
                        executed++;
                } else {
                    if (draw_polygon_fill(s, fr, cmd, &batch, points, point_count,
                                          op->u.fill_path.paint.color))
                        executed++;
                }
                continue;
            }
            if (sub_count == 1 &&
                fx_path_flatten_line_loop(fpath, 0.25f, &fr->arena,
                                          &flattened, &point_count)) {
                if (gradient) {
                    if (draw_gradient_polygon_fill(s, fr, cmd, &batch, flattened, point_count, gradient))
                        executed++;
                } else {
                    if (draw_polygon_fill(s, fr, cmd, &batch, flattened, point_count,
                                          op->u.fill_path.paint.color))
                        executed++;
                }
                continue;
            }
            /* General path: stencil-based fill for multi-subpath, holes, or open polylines */
            fx_rect path_bounds;
            if (fx_path_get_bounds(fpath, &path_bounds)) {
                flush_batch(s, cmd, &batch);

                /* Scissor to path bounds for stencil operations */
                VkRect2D path_scissor;
                {
                    int32_t x = (int32_t)roundf(path_bounds.x);
                    int32_t y = (int32_t)roundf(path_bounds.y);
                    int32_t w = (int32_t)roundf(path_bounds.w);
                    int32_t h = (int32_t)roundf(path_bounds.h);
                    if (x < 0) { w += x; x = 0; }
                    if (y < 0) { h += y; y = 0; }
                    if (x + w > (int32_t)s->extent.width)  w = (int32_t)s->extent.width - x;
                    if (y + h > (int32_t)s->extent.height) h = (int32_t)s->extent.height - y;
                    if (w < 0) w = 0;
                    if (h < 0) h = 0;
                    path_scissor.offset.x = x;
                    path_scissor.offset.y = y;
                    path_scissor.extent.width  = (uint32_t)w;
                    path_scissor.extent.height = (uint32_t)h;
                    vkCmdSetScissor(cmd, 0, 1, &path_scissor);
                }

                /* Clear stencil to 0 inside path bounds */
                {
                    VkClearAttachment clear = {
                        .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
                        .clearValue = { .depthStencil = { .stencil = 0 } },
                    };
                    VkClearRect crect = {
                        .rect = path_scissor,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    };
                    vkCmdClearAttachments(cmd, 1, &clear, 1, &crect);
                }

                /* Fill stencil pass: increment for each sub-path triangle */
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pc->fill_stencil_pipeline);
                set_viewport(s, cmd);
                vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0);

                fx_solid_color_pc pc = {
                    .surface_size = { (float)s->extent.width, (float)s->extent.height },
                };

                for (size_t si = 0; si < sub_count; ++si) {
                    if (!fx_path_flatten_subpath(fpath, si, 0.25f, &fr->arena,
                                                 &flattened, &point_count, &closed))
                        continue;
                    if (point_count < 3) continue;
                    fx_point *tris = NULL;
                    size_t tri_count = 0;
                    if (!fx_tessellate_simple_polygon(flattened, point_count, &fr->arena, &tris, &tri_count))
                        continue;
                    fx_solid_vertex *sv = fx_arena_alloc(&fr->arena, tri_count * sizeof(*sv));
                    if (!sv) continue;
                    for (size_t i = 0; i < tri_count; ++i)
                        sv[i] = (fx_solid_vertex){ .pos = { tris[i].x, tris[i].y } };
                    VkBuffer vbuf;
                    VkDeviceSize offset;
                    void *map = fx_vbuf_pool_alloc(&fr->vbuf, tri_count * sizeof(*sv), &vbuf, &offset);
                    if (!map) continue;
                    memcpy(map, sv, tri_count * sizeof(*sv));
                    vkCmdPushConstants(cmd, s->pc->stencil_layout,
                                       VK_SHADER_STAGE_VERTEX_BIT,
                                       0, sizeof(pc), &pc);
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &offset);
                    vkCmdDraw(cmd, (uint32_t)tri_count, 1, 0, 0);
                }

                /* Cover pass: draw a quad over path bounds where stencil == 1 */
                fx_solid_vertex cover_verts[6];
                cover_verts[0] = (fx_solid_vertex){ .pos = { path_bounds.x, path_bounds.y } };
                cover_verts[1] = (fx_solid_vertex){ .pos = { path_bounds.x + path_bounds.w, path_bounds.y } };
                cover_verts[2] = (fx_solid_vertex){ .pos = { path_bounds.x + path_bounds.w, path_bounds.y + path_bounds.h } };
                cover_verts[3] = (fx_solid_vertex){ .pos = { path_bounds.x, path_bounds.y } };
                cover_verts[4] = (fx_solid_vertex){ .pos = { path_bounds.x + path_bounds.w, path_bounds.y + path_bounds.h } };
                cover_verts[5] = (fx_solid_vertex){ .pos = { path_bounds.x, path_bounds.y + path_bounds.h } };

                VkBuffer cover_vbuf;
                VkDeviceSize cover_offset;
                void *cover_map = fx_vbuf_pool_alloc(&fr->vbuf, sizeof(cover_verts), &cover_vbuf, &cover_offset);
                if (cover_map) {
                    memcpy(cover_map, cover_verts, sizeof(cover_verts));

                    if (gradient) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pc->gradient_cover_pipeline);
                        set_viewport(s, cmd);
                        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 1);
                        fx_gradient_pc gpc = {
                            .surface_size = { (float)s->extent.width, (float)s->extent.height },
                            .mode = gradient->mode,
                            .stop_count = gradient->stop_count,
                            .start = { gradient->start[0], gradient->start[1] },
                            .end = { gradient->end[0], gradient->end[1] },
                        };
                        memcpy(gpc.colors, gradient->colors, sizeof(gpc.colors));
                        memcpy(gpc.stops, gradient->stops, sizeof(gpc.stops));
                        vkCmdPushConstants(cmd, s->pc->gradient_layout,
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, sizeof(gpc), &gpc);
                        vkCmdBindVertexBuffers(cmd, 0, 1, &cover_vbuf, &cover_offset);
                        vkCmdDraw(cmd, 6, 1, 0, 0);
                    } else {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pc->solid_cover_pipeline);
                        set_viewport(s, cmd);
                        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 1);
                        VkClearColorValue linear = to_clear_color(op->u.fill_path.paint.color, s->surface_format.format);
                        fx_solid_color_pc cpc = {
                            .surface_size = { (float)s->extent.width, (float)s->extent.height },
                            .pad = 0.0f,
                            .color = { linear.float32[0], linear.float32[1], linear.float32[2], linear.float32[3] },
                        };
                        vkCmdPushConstants(cmd, s->pc->solid_rect_layout,
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, sizeof(cpc), &cpc);
                        vkCmdBindVertexBuffers(cmd, 0, 1, &cover_vbuf, &cover_offset);
                        vkCmdDraw(cmd, 6, 1, 0, 0);
                    }
                    executed++;
                }

                /* Restore clip stencil if needed */
                if (active_clip_path) {
                    VkClearAttachment clear = {
                        .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
                        .clearValue = { .depthStencil = { .stencil = 0 } },
                    };
                    VkClearRect crect = {
                        .rect = path_scissor,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    };
                    vkCmdClearAttachments(cmd, 1, &clear, 1, &crect);
                    draw_clip_path_stencil(s, fr, cmd, active_clip_path);
                }

                /* Restore state for subsequent batching */
                bind_solid_pipeline(s, cmd);
                vkCmdSetScissor(cmd, 0, 1, &current_scissor);
                vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, current_stencil_ref);
            }
        } else if (op->kind == FX_OP_STROKE_PATH) {
            const fx_path *spath = op->u.stroke_path.path;
            size_t sub_count = fx_path_subpath_count(spath);
            if (sub_count == 1 &&
                op->u.stroke_path.paint.line_join == FX_JOIN_MITER &&
                fx_path_is_axis_aligned_rect(spath, &rect)) {
                if (draw_rect_stroke(s, fr, cmd, &batch, &rect,
                                     op->u.stroke_path.paint.stroke_width,
                                     op->u.stroke_path.paint.color))
                    executed++;
                continue;
            }
            bool any_stroked = false;
            for (size_t si = 0; si < sub_count; ++si) {
                if (!fx_path_flatten_subpath(spath, si, 0.25f, &fr->arena,
                                             &flattened, &point_count, &closed))
                    continue;
                if (fx_stroke_polyline(flattened, point_count, closed,
                                       &op->u.stroke_path.paint, &fr->arena,
                                       &stroke_tris, &stroke_count)) {
                    fx_solid_vertex *verts = (fx_solid_vertex *)stroke_tris;
                    if (add_to_batch(s, fr, cmd, &batch, op->u.stroke_path.paint.color,
                                     verts, stroke_count)) {
                        any_stroked = true;
                    }
                }
            }
            if (any_stroked) executed++;
        } else if (op->kind == FX_OP_DRAW_IMAGE) {
            if (draw_image_quad(s, fr, cmd, &batch, op->u.draw_image.image,
                                &op->u.draw_image.src, &op->u.draw_image.dst))
                executed++;
        } else if (op->kind == FX_OP_DRAW_GLYPHS) {
            if (draw_glyph_run(s, fr, cmd, &batch, &op->u.draw_glyphs))
                executed++;
        }
    }

    flush_batch(s, cmd, &batch);
    return executed;
}

fx_surface *fx_surface_create_vulkan(fx_context *ctx,
                                     VkSurfaceKHR vk_surface,
                                     int32_t width, int32_t height,
                                     fx_color_space cs)
{
    if (!ctx || vk_surface == VK_NULL_HANDLE) return NULL;

    fx_surface *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->ctx          = ctx;
    s->vk_surface   = vk_surface;
    s->requested_w  = width;
    s->requested_h  = height;
    s->color_space  = cs;
    s->canvas.owner = s;

    if (!ctx->device) {
        if (!fx_device_init(ctx, s->vk_surface)) {
            free(s);
            return NULL;
        }
    } else {
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(ctx->phys, ctx->graphics_family,
                                             s->vk_surface, &supported);
        if (!supported) {
            FX_LOGE(ctx, "device cannot present to this surface");
            free(s);
            return NULL;
        }
    }

    if (!fx_swapchain_build(s)) {
        free(s);
        return NULL;
    }
    return s;
}

fx_surface *fx_surface_create_wayland(fx_context *ctx,
                                      struct wl_display *display,
                                      struct wl_surface *wl_surface,
                                      int32_t width, int32_t height,
                                      fx_color_space cs)
{
    if (!ctx || !display || !wl_surface) return NULL;

    fx_surface *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->ctx          = ctx;
    s->requested_w  = width;
    s->requested_h  = height;
    s->color_space  = cs;
    s->canvas.owner = s;

    VkWaylandSurfaceCreateInfoKHR wsci = {
        .sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = display,
        .surface = wl_surface,
    };
    PFN_vkCreateWaylandSurfaceKHR create_fn =
        (PFN_vkCreateWaylandSurfaceKHR)
        vkGetInstanceProcAddr(ctx->instance, "vkCreateWaylandSurfaceKHR");
    if (!create_fn) {
        FX_LOGE(ctx, "vkCreateWaylandSurfaceKHR not exposed");
        free(s);
        return NULL;
    }
    if (create_fn(ctx->instance, &wsci, NULL, &s->vk_surface) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateWaylandSurfaceKHR failed");
        free(s);
        return NULL;
    }

    if (!ctx->device) {
        if (!fx_device_init(ctx, s->vk_surface)) {
            vkDestroySurfaceKHR(ctx->instance, s->vk_surface, NULL);
            free(s);
            return NULL;
        }
    } else {
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(ctx->phys, ctx->graphics_family,
                                             s->vk_surface, &supported);
        if (!supported) {
            FX_LOGE(ctx, "device cannot present to this wl_surface");
            vkDestroySurfaceKHR(ctx->instance, s->vk_surface, NULL);
            free(s);
            return NULL;
        }
    }

    if (!fx_swapchain_build(s)) {
        vkDestroySurfaceKHR(ctx->instance, s->vk_surface, NULL);
        free(s);
        return NULL;
    }
    return s;
}

fx_surface *fx_surface_create_offscreen(fx_context *ctx, int32_t width,
                                        int32_t height, fx_pixel_format format,
                                        fx_color_space cs)
{
    if (!ctx || width <= 0 || height <= 0) return NULL;

    fx_surface *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->ctx          = ctx;
    s->requested_w  = width;
    s->requested_h  = height;
    s->extent.width  = (uint32_t)width;
    s->extent.height = (uint32_t)height;
    s->color_space  = cs;
    s->canvas.owner = s;
    s->is_offscreen = true;

    VkFormat vkfmt = fx_pixel_format_to_vk(format);
    s->surface_format.format     = vkfmt;
    s->surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    /* Ensure device is initialized. Offscreen does not need a VkSurfaceKHR. */
    if (!ctx->device) {
        if (!fx_device_init(ctx, VK_NULL_HANDLE)) {
            FX_LOGE(ctx, "offscreen surface: device init failed");
            free(s);
            return NULL;
        }
    }

    VkImageCreateInfo ici = {
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = vkfmt,
        .extent      = { (uint32_t)width, (uint32_t)height, 1 },
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo offscreen_aci = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };
    VkResult r = vmaCreateImage(ctx->vma_allocator, &ici, &offscreen_aci,
                                &s->offscreen_image, &s->offscreen_alloc, NULL);
    if (r != VK_SUCCESS) {
        FX_LOGE(ctx, "vmaCreateImage (offscreen): %d", (int)r);
        free(s);
        return NULL;
    }

    VkImageViewCreateInfo vci = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = s->offscreen_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = vkfmt,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    r = vkCreateImageView(ctx->device, &vci, NULL, &s->offscreen_view);
    if (r != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateImageView (offscreen): %d", (int)r);
        goto fail;
    }

    /* Stencil attachment for offscreen clipping */
    VkImageCreateInfo sici = {
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = VK_FORMAT_S8_UINT,
        .extent      = { (uint32_t)width, (uint32_t)height, 1 },
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo stencil_aci = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };
    r = vmaCreateImage(ctx->vma_allocator, &sici, &stencil_aci,
                       &s->stencil_image, &s->stencil_alloc, NULL);
    if (r != VK_SUCCESS) {
        FX_LOGE(ctx, "vmaCreateImage (stencil): %d", (int)r);
        goto fail;
    }

    VkImageViewCreateInfo svci = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = s->stencil_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = VK_FORMAT_S8_UINT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    r = vkCreateImageView(ctx->device, &svci, NULL, &s->stencil_view);
    if (r != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateImageView (stencil): %d", (int)r);
        goto fail;
    }

    if (!fx_make_render_pass(s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)) goto fail;

    s->pc = fx_pipeline_set_get(s->ctx, vkfmt, VK_SAMPLE_COUNT_1_BIT);
    if (!s->pc) goto fail;

    VkImageView offscreen_attachments[2] = { s->offscreen_view, s->stencil_view };
    VkFramebufferCreateInfo fci = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = s->render_pass,
        .attachmentCount = 2,
        .pAttachments    = offscreen_attachments,
        .width           = (uint32_t)width,
        .height          = (uint32_t)height,
        .layers          = 1,
    };
    r = vkCreateFramebuffer(ctx->device, &fci, NULL, &s->offscreen_framebuffer);
    if (r != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateFramebuffer (offscreen): %d", (int)r);
        goto fail;
    }

    if (!fx_make_frames(s)) goto fail;

    FX_LOGI(ctx, "offscreen surface %dx%d format=%d", width, height, (int)vkfmt);
    return s;

fail:
    fx_surface_destroy(s);
    return NULL;
}

static bool readback_ensure(fx_surface *s, VkDeviceSize size)
{
    if (s->readback.capacity >= size && s->readback.buffer) return true;

    if (s->readback.mapped) {
        vmaUnmapMemory(s->ctx->vma_allocator, s->readback.alloc);
        s->readback.mapped = NULL;
    }
    if (s->readback.buffer) {
        vmaDestroyBuffer(s->ctx->vma_allocator, s->readback.buffer, s->readback.alloc);
        s->readback.buffer = VK_NULL_HANDLE;
        s->readback.alloc = VK_NULL_HANDLE;
    }

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo aci = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY,
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
    };
    VmaAllocationInfo ainfo;
    if (vmaCreateBuffer(s->ctx->vma_allocator, &bci, &aci,
                        &s->readback.buffer, &s->readback.alloc, &ainfo)
        != VK_SUCCESS) {
        return false;
    }
    s->readback.mapped = ainfo.pMappedData;
    s->readback.capacity = ainfo.size;
    return true;
}

bool fx_surface_read_pixels(fx_surface *s, void *data, size_t stride)
{
    if (!s || !s->is_offscreen || !data) return false;
    if (stride == 0) stride = s->extent.width * 4;

    fx_context *ctx = s->ctx;
    VkDeviceSize image_size = (VkDeviceSize)s->extent.height * stride;
    if (image_size == 0) return false;
    if (!readback_ensure(s, image_size)) return false;

    /* Wait for any in-flight render to finish before we read its output.
     * fx_surface_present no longer blocks for offscreen — read_pixels and
     * the next fx_surface_acquire own that synchronization. */
    fx_surface_wait_idle(s);

    /* Use the upload subsystem's cmd buffer and fence: image→buffer copy is
     * semantically a "GPU → host" transfer, and sharing these resources
     * avoids per-call VkCommandBuffer/VkFence allocation. */
    vkResetCommandBuffer(ctx->upload.cmd, 0);
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(ctx->upload.cmd, &bi);

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = s->offscreen_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(ctx->upload.cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);

    VkBufferImageCopy region = {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .imageExtent = { s->extent.width, s->extent.height, 1 },
    };
    vkCmdCopyImageToBuffer(ctx->upload.cmd, s->offscreen_image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           s->readback.buffer, 1, &region);

    vkEndCommandBuffer(ctx->upload.cmd);

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &ctx->upload.cmd,
    };
    vkResetFences(ctx->device, 1, &ctx->upload.fence);
    if (vkQueueSubmit(ctx->graphics_queue, 1, &si, ctx->upload.fence)
        != VK_SUCCESS) return false;
    if (vkWaitForFences(ctx->device, 1, &ctx->upload.fence, VK_TRUE,
                        UINT64_MAX) != VK_SUCCESS) return false;

    memcpy(data, s->readback.mapped, image_size);
    return true;
}

void fx_surface_resize(fx_surface *s, int32_t w, int32_t h)
{
    if (!s) return;
    s->requested_w   = w;
    s->requested_h   = h;
    if (w != (int32_t)s->extent.width || h != (int32_t)s->extent.height) {
        s->needs_recreate = true;
    }
}

static bool recreate_swapchain(fx_surface *s)
{
    fx_surface_wait_idle(s);
    vkDeviceWaitIdle(s->ctx->device);
    fx_swapchain_destroy(s);
    return fx_swapchain_build(s);
}

static void destroy_surface(fx_surface *s)
{
    VkDevice dev = s->ctx->device;

    if (s->is_offscreen) {
        for (uint32_t i = 0; i < FX_MAX_FRAMES_IN_FLIGHT; ++i) {
            if (s->frames[i].in_flight) {
                vkDestroyFence(dev, s->frames[i].in_flight, NULL);
                s->frames[i].in_flight = VK_NULL_HANDLE;
            }
            if (s->frames[i].image_available) {
                vkDestroySemaphore(dev, s->frames[i].image_available, NULL);
                s->frames[i].image_available = VK_NULL_HANDLE;
            }
            if (s->frames[i].render_finished) {
                vkDestroySemaphore(dev, s->frames[i].render_finished, NULL);
                s->frames[i].render_finished = VK_NULL_HANDLE;
            }
            if (s->frames[i].cmd) {
                vkFreeCommandBuffers(dev, s->ctx->frame_cmd_pool, 1,
                                     &s->frames[i].cmd);
                s->frames[i].cmd = VK_NULL_HANDLE;
            }
            fx_vbuf_pool_destroy(&s->frames[i].vbuf);
            fx_arena_destroy(&s->frames[i].arena);
            if (s->frames[i].desc_pool) {
                vkDestroyDescriptorPool(dev, s->frames[i].desc_pool, NULL);
                s->frames[i].desc_pool = VK_NULL_HANDLE;
            }
        }
        if (s->offscreen_framebuffer) {
            vkDestroyFramebuffer(dev, s->offscreen_framebuffer, NULL);
            s->offscreen_framebuffer = VK_NULL_HANDLE;
        }
        if (s->readback.buffer) {
            vmaDestroyBuffer(s->ctx->vma_allocator, s->readback.buffer, s->readback.alloc);
            s->readback.buffer = VK_NULL_HANDLE;
            s->readback.alloc = VK_NULL_HANDLE;
            s->readback.mapped = NULL;
        }
        if (s->stencil_view) {
            vkDestroyImageView(dev, s->stencil_view, NULL);
            s->stencil_view = VK_NULL_HANDLE;
        }
        if (s->stencil_image) {
            vmaDestroyImage(s->ctx->vma_allocator, s->stencil_image, s->stencil_alloc);
            s->stencil_image = VK_NULL_HANDLE;
            s->stencil_alloc = VK_NULL_HANDLE;
        }
        if (s->offscreen_view) {
            vkDestroyImageView(dev, s->offscreen_view, NULL);
            s->offscreen_view = VK_NULL_HANDLE;
        }
        if (s->offscreen_image) {
            vmaDestroyImage(s->ctx->vma_allocator, s->offscreen_image, s->offscreen_alloc);
            s->offscreen_image = VK_NULL_HANDLE;
            s->offscreen_alloc = VK_NULL_HANDLE;
        }
        fx_surface_destroy_pipelines(s);
    } else {
        fx_swapchain_destroy(s);
        fx_surface_destroy_pipelines(s);
        if (s->vk_surface) {
            vkDestroySurfaceKHR(s->ctx->instance, s->vk_surface, NULL);
            s->vk_surface = VK_NULL_HANDLE;
        }
    }
    fx_canvas_dispose(&s->canvas);
    free(s);
}

void fx_surface_destroy(fx_surface *s)
{
    if (!s) return;
    fx_surface_wait_idle(s);
    vkDeviceWaitIdle(s->ctx->device);
    destroy_surface(s);
}

void fx_surface_set_dpr(fx_surface *s, float dpr)
{
    if (!s || dpr <= 0.0f) return;
    s->canvas.dpr = dpr;
}

float fx_surface_get_dpr(const fx_surface *s)
{
    return s ? s->canvas.dpr : 1.0f;
}

fx_canvas *fx_surface_acquire(fx_surface *s)
{
    if (!s) return NULL;

    if (!s->is_offscreen) {
        if (s->needs_recreate) {
            if (!recreate_swapchain(s)) return NULL;
            if (s->needs_recreate) return NULL;  /* still zero extent */
        }
    }

    fx_frame *fr = &s->frames[s->frame_index];
    vkWaitForFences(s->ctx->device, 1, &fr->in_flight, VK_TRUE, UINT64_MAX);

    if (!s->is_offscreen) {
        VkResult ar = vkAcquireNextImageKHR(s->ctx->device, s->swapchain,
                                            UINT64_MAX,
                                            fr->image_available,
                                            VK_NULL_HANDLE,
                                            &s->acquired_image);
        if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
            s->needs_recreate = true;
            if (!recreate_swapchain(s)) return NULL;
            return fx_surface_acquire(s);
        }
        if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
            FX_LOGE(s->ctx, "vkAcquireNextImageKHR: %d", (int)ar);
            return NULL;
        }
    } else {
        s->acquired_image = 0;
    }

    vkResetFences(s->ctx->device, 1, &fr->in_flight);
    fx_vbuf_pool_reset(&fr->vbuf);
    fx_arena_reset(&fr->arena);
    vkResetDescriptorPool(s->ctx->device, fr->desc_pool, 0);
    fr->desc_cache_count = 0;

    /* Reset display list for the new frame. */
    fx_canvas_reset(&s->canvas);
    s->canvas.dpr = s->canvas.dpr > 0.0f ? s->canvas.dpr : 1.0f;
    return &s->canvas;
}

void fx_surface_present(fx_surface *s)
{
    if (!s) return;
    fx_frame *fr = &s->frames[s->frame_index];

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkResetCommandBuffer(fr->cmd, 0);
    vkBeginCommandBuffer(fr->cmd, &bi);

    VkClearValue clear_values[2] = { 0 };
    if (s->canvas.has_clear)
        clear_values[0].color = to_clear_color(s->canvas.clear_color,
                                               s->surface_format.format);
    /* Stencil attachment always clears to 0 at render pass begin. */

    VkRenderPassBeginInfo rpbi = {
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass  = s->is_offscreen ? s->render_pass : s->render_pass,
        .framebuffer = s->is_offscreen ? s->offscreen_framebuffer
                                       : s->images[s->acquired_image].framebuffer,
        .renderArea  = { .offset = {0,0}, .extent = s->extent },
        .clearValueCount = 2,
        .pClearValues    = clear_values,
    };
    vkCmdBeginRenderPass(fr->cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    record_ops(s, fr, fr->cmd);
    vkCmdEndRenderPass(fr->cmd);
    vkEndCommandBuffer(fr->cmd);

    if (s->is_offscreen) {
        VkSubmitInfo si = {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers    = &fr->cmd,
        };
        FX_LOG_VK(s->ctx, vkQueueSubmit(s->ctx->graphics_queue, 1, &si,
                                          fr->in_flight));
        /* No host wait: fx_surface_read_pixels, fx_surface_acquire (via the
         * per-frame fence wait), and fx_surface_destroy all synchronize
         * against fr->in_flight before they touch the image. */
    } else {
        VkPipelineStageFlags wait_stage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &fr->image_available,
            .pWaitDstStageMask    = &wait_stage,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &fr->cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = &fr->render_finished,
        };
        FX_LOG_VK(s->ctx, vkQueueSubmit(s->ctx->graphics_queue, 1, &si,
                                          fr->in_flight));

        VkPresentInfoKHR pi = {
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &fr->render_finished,
            .swapchainCount     = 1,
            .pSwapchains        = &s->swapchain,
            .pImageIndices      = &s->acquired_image,
        };
        VkResult pr = vkQueuePresentKHR(s->ctx->graphics_queue, &pi);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
            s->needs_recreate = true;
        } else if (pr != VK_SUCCESS) {
            FX_LOGE(s->ctx, "vkQueuePresentKHR: %d", (int)pr);
        }
    }

    s->frame_index = (s->frame_index + 1) % FX_MAX_FRAMES_IN_FLIGHT;
}
