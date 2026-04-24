#include "internal.h"

void fx_arena_init(fx_arena *arena, size_t block_size)
{
    arena->head = NULL;
    arena->block_size = block_size > 0 ? block_size : 65536;
}

void fx_arena_destroy(fx_arena *arena)
{
    fx_arena_block *curr = arena->head;
    while (curr) {
        fx_arena_block *next = curr->next;
        free(curr);
        curr = next;
    }
    arena->head = NULL;
}

void *fx_arena_alloc(fx_arena *arena, size_t size)
{
    /* Align to 8 bytes */
    size = (size + 7) & ~7;

    if (arena->head && (arena->head->used + size <= arena->head->size)) {
        void *ptr = arena->head->data + arena->head->used;
        arena->head->used += size;
        return ptr;
    }

    size_t alloc_size = size > arena->block_size ? size : arena->block_size;
    fx_arena_block *block = malloc(sizeof(fx_arena_block) + alloc_size);
    if (!block) return NULL;

    block->size = alloc_size;
    block->used = size;
    block->next = arena->head;
    arena->head = block;

    return block->data;
}

void fx_arena_reset(fx_arena *arena)
{
    fx_arena_block *curr = arena->head;
    if (!curr) return;

    /* Keep the first block, free the rest */
    while (curr->next) {
        fx_arena_block *next = curr->next->next;
        free(curr->next);
        curr->next = next;
    }

    arena->head->used = 0;
}
