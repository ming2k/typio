#include "internal.h"

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

static VkFormat to_vk_format(fx_pixel_format fmt)
{
    switch (fmt) {
        case FX_FMT_BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        case FX_FMT_RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case FX_FMT_A8_UNORM:    return VK_FORMAT_R8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

fx_image *fx_image_create(fx_context *ctx, const fx_image_desc *desc)
{
    fx_image *image;
    size_t bpp;
    size_t stride;
    size_t data_size = 0;

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
        data_size = stride * (size_t)desc->height;
        image->data = malloc(data_size);
        if (!image->data) {
            free(image);
            return NULL;
        }
        memcpy(image->data, desc->data, data_size);
        image->data_size = data_size;
    }

    /* Vulkan resource creation (skip if no device provided, e.g. in unit tests) */
    if (!ctx->device) return image;

    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = to_vk_format(desc->format),
        .extent = { desc->width, desc->height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(ctx->device, &ici, NULL, &image->vk_image) != VK_SUCCESS) {
        fx_image_destroy(image);
        return NULL;
    }

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(ctx->device, image->vk_image, &mr);

    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = find_memory_type(ctx, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    if (vkAllocateMemory(ctx->device, &mai, NULL, &image->vk_mem) != VK_SUCCESS) {
        fx_image_destroy(image);
        return NULL;
    }

    vkBindImageMemory(ctx->device, image->vk_image, image->vk_mem, 0);

    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->vk_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = ici.format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    if (vkCreateImageView(ctx->device, &vci, NULL, &image->vk_view) != VK_SUCCESS) {
        fx_image_destroy(image);
        return NULL;
    }

    /* Upload if data provided */
    if (image->data) {
        /* Create staging buffer */
        VkBuffer staging;
        VkDeviceMemory staging_mem;

        VkBufferCreateInfo bci = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = image->data_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCreateBuffer(ctx->device, &bci, NULL, &staging);
        
        VkMemoryRequirements bmr;
        vkGetBufferMemoryRequirements(ctx->device, staging, &bmr);
        
        VkMemoryAllocateInfo bmai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = bmr.size,
            .memoryTypeIndex = find_memory_type(ctx, bmr.memoryTypeBits, 
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkAllocateMemory(ctx->device, &bmai, NULL, &staging_mem);
        vkBindBufferMemory(ctx->device, staging, staging_mem, 0);

        void *mapped;
        vkMapMemory(ctx->device, staging_mem, 0, image->data_size, 0, &mapped);
        memcpy(mapped, image->data, image->data_size);
        vkUnmapMemory(ctx->device, staging_mem);

        /* Copy command */
        VkCommandBufferAllocateInfo cai = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = ctx->frame_cmd_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer cb;
        vkAllocateCommandBuffers(ctx->device, &cai, &cb);

        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        vkBeginCommandBuffer(cb, &bi);

        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .image = image->vk_image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

        VkBufferImageCopy region = {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { desc->width, desc->height, 1 },
        };
        vkCmdCopyBufferToImage(cb, staging, image->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

        vkEndCommandBuffer(cb);

        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
        vkQueueSubmit(ctx->graphics_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(ctx->graphics_queue);

        vkFreeCommandBuffers(ctx->device, ctx->frame_cmd_pool, 1, &cb);
        vkDestroyBuffer(ctx->device, staging, NULL);
        vkFreeMemory(ctx->device, staging_mem, NULL);
    }

    return image;
}

void fx_image_destroy(fx_image *image)
{
    if (!image) return;
    if (image->ctx->device) {
        if (image->vk_view) vkDestroyImageView(image->ctx->device, image->vk_view, NULL);
        if (image->vk_image) vkDestroyImage(image->ctx->device, image->vk_image, NULL);
        if (image->vk_mem) vkFreeMemory(image->ctx->device, image->vk_mem, NULL);
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

    fx_context *ctx = image->ctx;

    /* Create staging buffer */
    VkBuffer staging;
    VkDeviceMemory staging_mem;

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = image->data_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(ctx->device, &bci, NULL, &staging) != VK_SUCCESS) return false;
    
    VkMemoryRequirements bmr;
    vkGetBufferMemoryRequirements(ctx->device, staging, &bmr);
    
    VkMemoryAllocateInfo bmai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = bmr.size,
        .memoryTypeIndex = find_memory_type(ctx, bmr.memoryTypeBits, 
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    if (vkAllocateMemory(ctx->device, &bmai, NULL, &staging_mem) != VK_SUCCESS) {
        vkDestroyBuffer(ctx->device, staging, NULL);
        return false;
    }
    vkBindBufferMemory(ctx->device, staging, staging_mem, 0);

    void *mapped;
    vkMapMemory(ctx->device, staging_mem, 0, image->data_size, 0, &mapped);
    memcpy(mapped, image->data, image->data_size);
    vkUnmapMemory(ctx->device, staging_mem);

    /* Copy command */
    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->frame_cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(ctx->device, &cai, &cb);

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, /* Need to transition from current to TRANSFER_DST */
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .image = image->vk_image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

    VkBufferImageCopy region = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { image->desc.width, image->desc.height, 1 },
    };
    vkCmdCopyBufferToImage(cb, staging, image->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

    vkEndCommandBuffer(cb);

    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
    vkQueueSubmit(ctx->graphics_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->graphics_queue);

    vkFreeCommandBuffers(ctx->device, ctx->frame_cmd_pool, 1, &cb);
    vkDestroyBuffer(ctx->device, staging, NULL);
    vkFreeMemory(ctx->device, staging_mem, NULL);

    return true;
}
