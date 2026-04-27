#include "internal.h"
#include "vk/vk_mem_alloc.h"

static size_t bytes_per_pixel(fx_pixel_format format)
{
    switch (format) {
        case FX_FMT_BGRA8_UNORM:
        case FX_FMT_RGBA8_UNORM:
            return 4;
        case FX_FMT_A8_UNORM:
            return 1;
    }
    return 0;
}

VkFormat fx_pixel_format_to_vk(fx_pixel_format fmt)
{
    switch (fmt) {
        case FX_FMT_BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        case FX_FMT_RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case FX_FMT_A8_UNORM:    return VK_FORMAT_R8_UNORM;
    }
    return VK_FORMAT_B8G8R8A8_UNORM;
}

/* Transition a freshly created image from UNDEFINED to SHADER_READ_ONLY_OPTIMAL
 * when no initial upload is supplied. Uses the context's upload cmd buffer and
 * fence so we avoid the per-op fence allocation. */
static bool layout_to_shader_read(fx_context *ctx, VkImage image)
{
    if (!ctx->upload.cmd || !ctx->upload.fence) return false;

    vkResetCommandBuffer(ctx->upload.cmd, 0);
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(ctx->upload.cmd, &bi);

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(ctx->upload.cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    vkEndCommandBuffer(ctx->upload.cmd);

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &ctx->upload.cmd,
    };
    vkResetFences(ctx->device, 1, &ctx->upload.fence);
    if (vkQueueSubmit(ctx->graphics_queue, 1, &si, ctx->upload.fence)
        != VK_SUCCESS) return false;
    return vkWaitForFences(ctx->device, 1, &ctx->upload.fence, VK_TRUE,
                           UINT64_MAX) == VK_SUCCESS;
}

fx_image *fx_image_create(fx_context *ctx, const fx_image_desc *desc)
{
    fx_image *image;
    size_t bpp;
    size_t stride;

    if (!ctx || !desc || desc->width == 0 || desc->height == 0) return NULL;

    bpp = bytes_per_pixel(desc->format);
    if (!bpp) return NULL;

    stride = desc->stride ? desc->stride : (size_t)desc->width * bpp;
    if (stride < (size_t)desc->width * bpp) return NULL;

    image = calloc(1, sizeof(*image));
    if (!image) return NULL;

    image->ctx = ctx;
    image->desc = *desc;
    image->desc.data = NULL;
    image->desc.stride = stride;
    if (image->desc.usage == 0) {
        image->desc.usage = FX_IMAGE_USAGE_SAMPLED | FX_IMAGE_USAGE_TRANSFER_DST;
    }

    if (desc->data) {
        size_t data_size = stride * (size_t)desc->height;
        image->data = malloc(data_size);
        if (!image->data) {
            free(image);
            return NULL;
        }
        memcpy(image->data, desc->data, data_size);
        image->data_size = data_size;
    }

    /* Skip Vulkan resource creation when no device is available (unit tests). */
    if (!ctx->device) return image;

    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = fx_pixel_format_to_vk(desc->format),
        .extent = { desc->width, desc->height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo aci = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };
    if (vmaCreateImage(ctx->vma_allocator, &ici, &aci,
                       &image->vk_image, &image->vma_alloc, NULL) != VK_SUCCESS) {
        FX_LOGE(ctx, "vmaCreateImage failed");
        fx_image_destroy(image);
        return NULL;
    }

    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->vk_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = ici.format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(ctx->device, &vci, NULL, &image->vk_view) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateImageView failed");
        fx_image_destroy(image);
        return NULL;
    }

    if (image->data) {
        if (!fx_upload_image(ctx, image->vk_image,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             0, 0, desc->width, desc->height,
                             image->data, image->desc.stride, bpp)) {
            FX_LOGE(ctx, "initial image upload failed");
            fx_image_destroy(image);
            return NULL;
        }
    } else if (!layout_to_shader_read(ctx, image->vk_image)) {
        FX_LOGE(ctx, "image layout transition failed");
        fx_image_destroy(image);
        return NULL;
    }

    return image;
}

void fx_image_destroy(fx_image *image)
{
    if (!image) return;
    if (image->ctx->device) {
        if (image->vk_view) vkDestroyImageView(image->ctx->device, image->vk_view, NULL);
        if (image->vk_image) vmaDestroyImage(image->ctx->vma_allocator, image->vk_image, image->vma_alloc);
    }
    free(image->data);
    free(image);
}

bool fx_image_get_desc(const fx_image *image, fx_image_desc *out_desc)
{
    if (!image) return false;
    if (out_desc) {
        *out_desc = image->desc;
        out_desc->data = NULL;
    }
    return true;
}

const void *fx_image_data(const fx_image *image,
                          size_t *out_size,
                          size_t *out_stride)
{
    if (!image) return NULL;
    if (out_size) *out_size = image->data_size;
    if (out_stride) *out_stride = image->desc.stride;
    return image->data;
}

bool fx_image_update(fx_image *image, const void *data, size_t stride)
{
    if (!image || !data) return false;

    size_t bpp = bytes_per_pixel(image->desc.format);
    if (!bpp) return false;

    size_t expected_stride = stride ? stride : (size_t)image->desc.width * bpp;
    if (expected_stride < (size_t)image->desc.width * bpp) return false;

    size_t new_data_size = expected_stride * (size_t)image->desc.height;

    if (!image->data || new_data_size != image->data_size) {
        void *new_data = realloc(image->data, new_data_size);
        if (!new_data) return false;
        image->data = new_data;
        image->data_size = new_data_size;
    }

    memcpy(image->data, data, new_data_size);
    image->desc.stride = expected_stride;

    if (!image->ctx->device) return true;

    /* Wait for any in-flight frame that is still sampling this image. */
    if (image->last_use_fence != VK_NULL_HANDLE) {
        vkWaitForFences(image->ctx->device, 1, &image->last_use_fence,
                        VK_TRUE, UINT64_MAX);
        image->last_use_fence = VK_NULL_HANDLE;
    }

    /* The image is in SHADER_READ_ONLY_OPTIMAL from the last upload (either
     * initial or the previous fx_image_update), so transition from that. */
    return fx_upload_image(image->ctx, image->vk_image,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           0, 0, image->desc.width, image->desc.height,
                           image->data, image->desc.stride, bpp);
}
