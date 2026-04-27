#include "memory.h"
#include "vk/vk_mem_alloc.h"
#include "../internal.h"

#define INITIAL_CHUNK_SIZE (4 * 1024 * 1024)

static fx_vbuf_chunk *chunk_create(fx_context *ctx, size_t size)
{
    fx_vbuf_chunk *chunk = calloc(1, sizeof(*chunk));
    if (!chunk) return NULL;

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo aci = {
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
    };
    VmaAllocationInfo ainfo;
    if (vmaCreateBuffer(ctx->vma_allocator, &bci, &aci,
                        &chunk->buffer, &chunk->alloc, &ainfo) != VK_SUCCESS) {
        free(chunk);
        return NULL;
    }

    chunk->map = ainfo.pMappedData;
    chunk->size = ainfo.size;
    return chunk;
}

void fx_vbuf_pool_init(fx_vbuf_pool *pool, fx_context *ctx)
{
    pool->ctx = ctx;
    pool->head = NULL;
    pool->cursor = 0;
    pool->next_size = INITIAL_CHUNK_SIZE;
}

void fx_vbuf_pool_destroy(fx_vbuf_pool *pool)
{
    fx_vbuf_chunk *chunk = pool->head;
    while (chunk) {
        fx_vbuf_chunk *next = chunk->next;
        vmaDestroyBuffer(pool->ctx->vma_allocator, chunk->buffer, chunk->alloc);
        free(chunk);
        chunk = next;
    }
    pool->head = NULL;
}

void fx_vbuf_pool_reset(fx_vbuf_pool *pool)
{
    pool->cursor = 0;
}

void *fx_vbuf_pool_alloc(fx_vbuf_pool *pool, size_t size,
                         VkBuffer *out_buffer, VkDeviceSize *out_offset)
{
    /* Align to 16 bytes for sanity */
    size = (size + 15) & ~15;

    if (!pool->head || pool->cursor + size > pool->head->size) {
        size_t new_size = pool->next_size;
        if (size > new_size) new_size = size;

        fx_vbuf_chunk *new_chunk = chunk_create(pool->ctx, new_size);
        if (!new_chunk) return NULL;

        new_chunk->next = pool->head;
        pool->head = new_chunk;
        pool->cursor = 0;
        pool->next_size = new_size * 2;
    }

    void *ptr = (char *)pool->head->map + pool->cursor;
    if (out_buffer) *out_buffer = pool->head->buffer;
    if (out_offset) *out_offset = (VkDeviceSize)pool->cursor;

    pool->cursor += size;
    return ptr;
}
