#ifndef FX_VK_MEMORY_H
#define FX_VK_MEMORY_H

#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stddef.h>

VK_DEFINE_HANDLE(VmaAllocation)

typedef struct fx_context fx_context;

typedef struct fx_vbuf_chunk {
    VkBuffer        buffer;
    VmaAllocation   alloc;
    void           *map;
    size_t          size;
    struct fx_vbuf_chunk *next;
} fx_vbuf_chunk;

typedef struct {
    fx_context    *ctx;
    fx_vbuf_chunk *head;
    size_t         cursor;
    size_t         next_size;
} fx_vbuf_pool;

void fx_vbuf_pool_init(fx_vbuf_pool *pool, fx_context *ctx);
void fx_vbuf_pool_destroy(fx_vbuf_pool *pool);
void fx_vbuf_pool_reset(fx_vbuf_pool *pool);

/*
 * Allocates 'size' bytes from the pool.
 * Returns the mapped pointer, and fills 'out_buffer' and 'out_offset'.
 * Returns NULL on failure.
 */
void *fx_vbuf_pool_alloc(fx_vbuf_pool *pool, size_t size,
                         VkBuffer *out_buffer, VkDeviceSize *out_offset);

#endif /* FX_VK_MEMORY_H */
