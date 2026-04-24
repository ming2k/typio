#include "flux/flux_wayland.h"
#include "flux/flux_vulkan.h"
#include "internal.h"

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

static VkFormat pixel_format_to_vk(fx_pixel_format fmt)
{
    switch (fmt) {
    case FX_FMT_BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
    case FX_FMT_RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
    case FX_FMT_A8_UNORM:    return VK_FORMAT_R8_UNORM;
    default:                 return VK_FORMAT_B8G8R8A8_UNORM;
    }
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
                      s->solid_rect_pipeline);
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

    vkCmdPushConstants(cmd, s->solid_rect_layout,
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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->gradient_pipeline);
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

    vkCmdPushConstants(cmd, s->gradient_layout,
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

    /* Descriptor Set */
    VkDescriptorSetAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = fr->desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &s->image_dsl,
    };
    VkDescriptorSet ds;
    if (vkAllocateDescriptorSets(s->ctx->device, &ai, &ds) != VK_SUCCESS) return false;

    VkDescriptorImageInfo dii = {
        .sampler = s->sampler,
        .imageView = image->vk_view,
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
    vkUpdateDescriptorSets(s->ctx->device, 1, &write, 0, NULL);

    /* Draw */
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->image_pipeline);
    set_viewport(s, cmd);
    
    fx_image_pc pc = { .surface_size = { (float)s->extent.width, (float)s->extent.height } };
    vkCmdPushConstants(cmd, s->image_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->image_layout, 0, 1, &ds, 0, NULL);
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

    /* Descriptor Set for Atlas */
    VkDescriptorSetAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = fr->desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &s->image_dsl,
    };
    VkDescriptorSet ds;
    if (vkAllocateDescriptorSets(s->ctx->device, &ai, &ds) != VK_SUCCESS) return false;

    VkDescriptorImageInfo dii = {
        .sampler = s->sampler,
        .imageView = s->ctx->atlas.image->vk_view,
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
    vkUpdateDescriptorSets(s->ctx->device, 1, &write, 0, NULL);

    /* Draw */
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->text_pipeline);
    set_viewport(s, cmd);
    
    VkClearColorValue linear = to_clear_color(op->paint.color, s->surface_format.format);
    fx_text_pc pc = {
        .surface_size = { (float)s->extent.width, (float)s->extent.height },
        .color = { linear.float32[0], linear.float32[1], linear.float32[2], linear.float32[3] },
    };
    vkCmdPushConstants(cmd, s->text_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->text_layout, 0, 1, &ds, 0, NULL);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &offset);
    vkCmdDraw(cmd, (uint32_t)active_count * 6, 1, 0, 0);

    /* Re-bind solid pipeline */
    bind_solid_pipeline(s, cmd);
    return true;
}

static size_t record_bootstrap_ops(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd)
{
    size_t executed = 0;
    fx_batch batch = { .active = false };
    VkRect2D full_scissor = { .offset = {0, 0}, .extent = s->extent };
    VkRect2D current_scissor = full_scissor;
    uint32_t current_stencil_ref = 0;

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

                VkClearAttachment clear = {
                    .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
                    .clearValue = { .depthStencil = { .stencil = 1 } },
                };
                VkClearRect crect = {
                    .rect = current_scissor,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                };
                vkCmdClearAttachments(cmd, 1, &clear, 1, &crect);
            }
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

        if (op->kind == FX_OP_FILL_PATH) {
            const fx_gradient *gradient = op->u.fill_path.paint.gradient;
            if (fx_path_is_axis_aligned_rect(op->u.fill_path.path, &rect)) {
                if (gradient) {
                    if (draw_gradient_rect(s, fr, cmd, &batch, &rect, gradient))
                        executed++;
                } else {
                    if (draw_solid_rect(s, fr, cmd, &batch, &rect, op->u.fill_path.paint.color))
                        executed++;
                }
                continue;
            }
            if (fx_path_get_line_loop(op->u.fill_path.path,
                                      &points, &point_count)) {
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
            if (fx_path_flatten_line_loop(op->u.fill_path.path, 0.25f, &fr->arena,
                                          &flattened, &point_count)) {
                if (gradient) {
                    if (draw_gradient_polygon_fill(s, fr, cmd, &batch, flattened, point_count, gradient))
                        executed++;
                } else {
                    if (draw_polygon_fill(s, fr, cmd, &batch, flattened, point_count,
                                          op->u.fill_path.paint.color))
                        executed++;
                }
            }
        } else if (op->kind == FX_OP_STROKE_PATH) {
            if (op->u.stroke_path.paint.line_join == FX_JOIN_MITER &&
                fx_path_is_axis_aligned_rect(op->u.stroke_path.path, &rect)) {
                if (draw_rect_stroke(s, fr, cmd, &batch, &rect,
                                     op->u.stroke_path.paint.stroke_width,
                                     op->u.stroke_path.paint.color))
                    executed++;
                continue;
            }
            if (!fx_path_flatten_polyline(op->u.stroke_path.path, 0.25f, &fr->arena,
                                          &flattened, &point_count, &closed))
                continue;
            if (fx_stroke_polyline(flattened, point_count, closed,
                                   &op->u.stroke_path.paint, &fr->arena,
                                   &stroke_tris, &stroke_count)) {
                fx_solid_vertex *verts = (fx_solid_vertex *)stroke_tris;
                if (add_to_batch(s, fr, cmd, &batch, op->u.stroke_path.paint.color,
                                 verts, stroke_count)) {
                    executed++;
                }
            }
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

uint32_t find_memory_type(fx_context *ctx, uint32_t type_filter,
                                  VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem = ctx->mem_props;
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return 0;
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

    VkFormat vkfmt = pixel_format_to_vk(format);
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
    VkResult r = vkCreateImage(ctx->device, &ici, NULL, &s->offscreen_image);
    if (r != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateImage (offscreen): %d", (int)r);
        free(s);
        return NULL;
    }

    VkMemoryRequirements memreq;
    vkGetImageMemoryRequirements(ctx->device, s->offscreen_image, &memreq);
    VkMemoryAllocateInfo mai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memreq.size,
        .memoryTypeIndex = find_memory_type(ctx, memreq.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    r = vkAllocateMemory(ctx->device, &mai, NULL, &s->offscreen_memory);
    if (r != VK_SUCCESS) {
        FX_LOGE(ctx, "vkAllocateMemory (offscreen): %d", (int)r);
        vkDestroyImage(ctx->device, s->offscreen_image, NULL);
        free(s);
        return NULL;
    }
    vkBindImageMemory(ctx->device, s->offscreen_image, s->offscreen_memory, 0);

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
    r = vkCreateImage(ctx->device, &sici, NULL, &s->stencil_image);
    if (r != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateImage (stencil): %d", (int)r);
        goto fail;
    }
    vkGetImageMemoryRequirements(ctx->device, s->stencil_image, &memreq);
    mai.allocationSize  = memreq.size;
    mai.memoryTypeIndex = find_memory_type(ctx, memreq.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    r = vkAllocateMemory(ctx->device, &mai, NULL, &s->stencil_memory);
    if (r != VK_SUCCESS) {
        FX_LOGE(ctx, "vkAllocateMemory (stencil): %d", (int)r);
        goto fail;
    }
    vkBindImageMemory(ctx->device, s->stencil_image, s->stencil_memory, 0);

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

    if (!fx_make_image_dsl(s)) goto fail;
    if (!fx_make_image_pipeline(s)) goto fail;
    if (!fx_make_text_pipeline(s)) goto fail;
    if (!fx_make_gradient_pipeline(s)) goto fail;
    if (!fx_make_bootstrap_pipeline(s)) goto fail;
    if (!fx_make_stencil_pipeline(s)) goto fail;

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

bool fx_surface_read_pixels(fx_surface *s, void *data, size_t stride)
{
    if (!s || !s->is_offscreen || !data) return false;
    if (stride == 0) stride = s->extent.width * 4;

    fx_context *ctx = s->ctx;
    VkDeviceSize image_size = s->extent.height * stride;

    /* Create a staging buffer. */
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = image_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer staging = VK_NULL_HANDLE;
    VkResult r = vkCreateBuffer(ctx->device, &bci, NULL, &staging);
    if (r != VK_SUCCESS) return false;

    VkMemoryRequirements memreq;
    vkGetBufferMemoryRequirements(ctx->device, staging, &memreq);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memreq.size,
        .memoryTypeIndex = find_memory_type(ctx, memreq.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    r = vkAllocateMemory(ctx->device, &mai, NULL, &staging_mem);
    if (r != VK_SUCCESS) {
        vkDestroyBuffer(ctx->device, staging, NULL);
        return false;
    }
    vkBindBufferMemory(ctx->device, staging, staging_mem, 0);

    /* Copy image to buffer. */
    VkCommandBufferAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->frame_cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(ctx->device, &ai, &cmd);

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &bi);

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
    vkCmdPipelineBarrier(cmd,
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
    vkCmdCopyImageToBuffer(cmd, s->offscreen_image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    vkCreateFence(ctx->device, &fci, NULL, &fence);
    vkQueueSubmit(ctx->graphics_queue, 1, &si, fence);
    vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, UINT64_MAX);

    void *mapped = NULL;
    vkMapMemory(ctx->device, staging_mem, 0, image_size, 0, &mapped);
    memcpy(data, mapped, image_size);
    vkUnmapMemory(ctx->device, staging_mem);

    vkDestroyFence(ctx->device, fence, NULL);
    vkFreeCommandBuffers(ctx->device, ctx->frame_cmd_pool, 1, &cmd);
    vkFreeMemory(ctx->device, staging_mem, NULL);
    vkDestroyBuffer(ctx->device, staging, NULL);
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
    if (s->is_offscreen) {
        VkDevice dev = s->ctx->device;
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
        if (s->sampler) {
            vkDestroySampler(dev, s->sampler, NULL);
            s->sampler = VK_NULL_HANDLE;
        }
        if (s->offscreen_framebuffer) {
            vkDestroyFramebuffer(dev, s->offscreen_framebuffer, NULL);
            s->offscreen_framebuffer = VK_NULL_HANDLE;
        }
        if (s->solid_rect_pipeline) {
            vkDestroyPipeline(dev, s->solid_rect_pipeline, NULL);
            s->solid_rect_pipeline = VK_NULL_HANDLE;
        }
        if (s->solid_rect_layout) {
            vkDestroyPipelineLayout(dev, s->solid_rect_layout, NULL);
            s->solid_rect_layout = VK_NULL_HANDLE;
        }
        if (s->image_pipeline) {
            vkDestroyPipeline(dev, s->image_pipeline, NULL);
            s->image_pipeline = VK_NULL_HANDLE;
        }
        if (s->image_layout) {
            vkDestroyPipelineLayout(dev, s->image_layout, NULL);
            s->image_layout = VK_NULL_HANDLE;
        }
        if (s->text_pipeline) {
            vkDestroyPipeline(dev, s->text_pipeline, NULL);
            s->text_pipeline = VK_NULL_HANDLE;
        }
        if (s->text_layout) {
            vkDestroyPipelineLayout(dev, s->text_layout, NULL);
            s->text_layout = VK_NULL_HANDLE;
        }
        if (s->blur_pipeline) {
            vkDestroyPipeline(dev, s->blur_pipeline, NULL);
            s->blur_pipeline = VK_NULL_HANDLE;
        }
        if (s->blur_layout) {
            vkDestroyPipelineLayout(dev, s->blur_layout, NULL);
            s->blur_layout = VK_NULL_HANDLE;
        }
        if (s->stencil_pipeline) {
            vkDestroyPipeline(dev, s->stencil_pipeline, NULL);
            s->stencil_pipeline = VK_NULL_HANDLE;
        }
        if (s->stencil_layout) {
            vkDestroyPipelineLayout(dev, s->stencil_layout, NULL);
            s->stencil_layout = VK_NULL_HANDLE;
        }
        if (s->image_dsl) {
            vkDestroyDescriptorSetLayout(dev, s->image_dsl, NULL);
            s->image_dsl = VK_NULL_HANDLE;
        }
        if (s->render_pass) {
            vkDestroyRenderPass(dev, s->render_pass, NULL);
            s->render_pass = VK_NULL_HANDLE;
        }
        if (s->stencil_view) {
            vkDestroyImageView(dev, s->stencil_view, NULL);
            s->stencil_view = VK_NULL_HANDLE;
        }
        if (s->stencil_image) {
            vkDestroyImage(dev, s->stencil_image, NULL);
            s->stencil_image = VK_NULL_HANDLE;
        }
        if (s->stencil_memory) {
            vkFreeMemory(dev, s->stencil_memory, NULL);
            s->stencil_memory = VK_NULL_HANDLE;
        }
        if (s->offscreen_view) {
            vkDestroyImageView(dev, s->offscreen_view, NULL);
            s->offscreen_view = VK_NULL_HANDLE;
        }
        if (s->offscreen_image) {
            vkDestroyImage(dev, s->offscreen_image, NULL);
            s->offscreen_image = VK_NULL_HANDLE;
        }
        if (s->offscreen_memory) {
            vkFreeMemory(dev, s->offscreen_memory, NULL);
            s->offscreen_memory = VK_NULL_HANDLE;
        }
    } else {
        fx_swapchain_destroy(s);
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

    VkClearValue cv = { 0 };
    if (s->canvas.has_clear)
        cv.color = to_clear_color(s->canvas.clear_color,
                                  s->surface_format.format);

    VkRenderPassBeginInfo rpbi = {
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass  = s->is_offscreen ? s->render_pass : s->render_pass,
        .framebuffer = s->is_offscreen ? s->offscreen_framebuffer
                                       : s->images[s->acquired_image].framebuffer,
        .renderArea  = { .offset = {0,0}, .extent = s->extent },
        .clearValueCount = 1,
        .pClearValues    = &cv,
    };
    vkCmdBeginRenderPass(fr->cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    size_t executed_ops = record_bootstrap_ops(s, fr, fr->cmd);
    if (s->canvas.op_count > executed_ops) {
        FX_LOGI(s->ctx,
                "executed %zu/%zu recorded canvas ops; remaining ops still "
                "wait for the Vulkan raster backend",
                executed_ops, s->canvas.op_count);
    }
    vkCmdEndRenderPass(fr->cmd);
    vkEndCommandBuffer(fr->cmd);

    if (s->is_offscreen) {
        VkSubmitInfo si = {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers    = &fr->cmd,
        };
        FX_CHECK_VK(s->ctx, vkQueueSubmit(s->ctx->graphics_queue, 1, &si,
                                          fr->in_flight));
        /* For offscreen, we block here so the image is immediately readable.
         * Callers can also call fx_surface_read_pixels which does its own wait. */
        vkWaitForFences(s->ctx->device, 1, &fr->in_flight, VK_TRUE, UINT64_MAX);
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
        FX_CHECK_VK(s->ctx, vkQueueSubmit(s->ctx->graphics_queue, 1, &si,
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
