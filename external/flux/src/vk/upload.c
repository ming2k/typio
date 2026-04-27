/* Unified GPU upload path.
 *
 * Replaces the repeated "create staging buffer + allocate memory + submit
 * one-shot cmd + vkQueueWaitIdle + destroy everything" pattern with a single
 * per-context subsystem that owns reusable staging memory, a command buffer
 * and a fence.
 *
 * The public entrypoints block until the upload completes. A future step can
 * layer deferred submission on top: the one-shot wait is the wrong place to
 * optimize first because it is dominated by the cost of re-allocating host
 * memory and creating a fence per call, which this file eliminates. */

#include "../internal.h"
#include "vk/vk_mem_alloc.h"

/* Growth policy: round requested size up to the next power-of-two, clamped
 * to a lower bound to avoid thrashing on tiny glyph uploads. */
#define FX_UPLOAD_MIN_STAGING (64 * 1024)

static size_t next_pow2(size_t x)
{
    if (x < 2) return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
#if SIZE_MAX > UINT32_MAX
    x |= x >> 32;
#endif
    return x + 1;
}

static bool ensure_staging(fx_context *ctx, size_t size)
{
    if (ctx->upload.staging_size >= size) return true;

    if (ctx->upload.staging_buffer) {
        vmaDestroyBuffer(ctx->vma_allocator, ctx->upload.staging_buffer,
                         ctx->upload.staging_alloc);
        ctx->upload.staging_buffer = VK_NULL_HANDLE;
        ctx->upload.staging_alloc = VK_NULL_HANDLE;
        ctx->upload.staging_mapped = NULL;
    }

    size_t want = next_pow2(size);
    if (want < FX_UPLOAD_MIN_STAGING) want = FX_UPLOAD_MIN_STAGING;

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = want,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo aci = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY,
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
    };
    VmaAllocationInfo ainfo;
    if (vmaCreateBuffer(ctx->vma_allocator, &bci, &aci,
                        &ctx->upload.staging_buffer, &ctx->upload.staging_alloc,
                        &ainfo) != VK_SUCCESS) {
        return false;
    }

    ctx->upload.staging_mapped = ainfo.pMappedData;
    ctx->upload.staging_size = ainfo.size;
    return true;
}

bool fx_upload_init(fx_context *ctx)
{
    if (!ctx->device) return false;

    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->frame_cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(ctx->device, &cai, &ctx->upload.cmd)
        != VK_SUCCESS) {
        return false;
    }

    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(ctx->device, &fci, NULL, &ctx->upload.fence)
        != VK_SUCCESS) {
        vkFreeCommandBuffers(ctx->device, ctx->frame_cmd_pool, 1,
                             &ctx->upload.cmd);
        ctx->upload.cmd = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void fx_upload_shutdown(fx_context *ctx)
{
    if (!ctx->device) return;
    if (ctx->upload.staging_buffer) {
        vmaDestroyBuffer(ctx->vma_allocator, ctx->upload.staging_buffer,
                         ctx->upload.staging_alloc);
        ctx->upload.staging_buffer = VK_NULL_HANDLE;
        ctx->upload.staging_alloc = VK_NULL_HANDLE;
        ctx->upload.staging_mapped = NULL;
    }
    if (ctx->upload.fence) {
        vkDestroyFence(ctx->device, ctx->upload.fence, NULL);
        ctx->upload.fence = VK_NULL_HANDLE;
    }
    if (ctx->upload.cmd) {
        vkFreeCommandBuffers(ctx->device, ctx->frame_cmd_pool, 1,
                             &ctx->upload.cmd);
        ctx->upload.cmd = VK_NULL_HANDLE;
    }
    ctx->upload.staging_size = 0;
}

bool fx_upload_image(fx_context *ctx, VkImage image,
                     VkImageLayout old_layout, VkImageLayout new_layout,
                     int32_t dst_x, int32_t dst_y,
                     uint32_t w, uint32_t h,
                     const void *data, size_t row_bytes, size_t bpp)
{
    if (!ctx || !ctx->device || !data || !image) return false;
    if (!w || !h || !row_bytes || !bpp) return false;

    size_t dense = (size_t)w * bpp;
    if (row_bytes < dense) return false;

    size_t total = dense * (size_t)h;
    if (!ensure_staging(ctx, total)) return false;

    uint8_t *dst = (uint8_t *)ctx->upload.staging_mapped;
    const uint8_t *src = data;
    if (row_bytes == dense) {
        memcpy(dst, src, total);
    } else {
        for (uint32_t row = 0; row < h; ++row) {
            memcpy(dst + row * dense, src + row * row_bytes, dense);
        }
    }

    vkResetCommandBuffer(ctx->upload.cmd, 0);
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(ctx->upload.cmd, &bi);

    VkAccessFlags src_access = 0;
    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        src_access = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        src_access = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }

    VkImageMemoryBarrier to_dst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcAccessMask = src_access,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(ctx->upload.cmd, src_stage,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &to_dst);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = { dst_x, dst_y, 0 },
        .imageExtent = { w, h, 1 },
    };
    vkCmdCopyBufferToImage(ctx->upload.cmd, ctx->upload.staging_buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier to_final = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = new_layout,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(ctx->upload.cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &to_final);

    vkEndCommandBuffer(ctx->upload.cmd);

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &ctx->upload.cmd,
    };
    vkResetFences(ctx->device, 1, &ctx->upload.fence);
    if (vkQueueSubmit(ctx->graphics_queue, 1, &si, ctx->upload.fence)
        != VK_SUCCESS) {
        return false;
    }
    if (vkWaitForFences(ctx->device, 1, &ctx->upload.fence, VK_TRUE,
                        UINT64_MAX) != VK_SUCCESS) {
        return false;
    }
    return true;
}
