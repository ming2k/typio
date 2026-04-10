#include "arena.h"

#include <stdlib.h>
#include <string.h>

/* Alignment requirement (8 bytes for typical 64-bit systems) */
#define ARENA_ALIGNMENT 8
#define ALIGN_UP(size, align) (((size) + (size_t)(align) - 1) & ~((size_t)(align) - 1))

struct TypioArenaBlock {
    struct TypioArenaBlock *next;
    size_t used;
    size_t capacity;
    uint8_t data[]; /* Flexible array member */
};

static TypioArenaBlock *allocate_block(size_t capacity) {
    TypioArenaBlock *block = calloc(1, sizeof(TypioArenaBlock) + capacity);
    if (!block) return NULL;
    
    block->capacity = capacity;
    block->used = 0;
    block->next = NULL;
    return block;
}

bool typio_arena_init(TypioArena *arena, size_t block_size) {
    if (!arena) return false;
    
    /* Default to 4KB if requested size is too small */
    if (block_size < 4096) {
        block_size = 4096;
    }
    
    arena->block_size = block_size;
    arena->current = allocate_block(block_size);
    return arena->current != NULL;
}

void *typio_arena_alloc(TypioArena *arena, size_t size) {
    if (!arena || !arena->current) return NULL;

    size = ALIGN_UP(size, ARENA_ALIGNMENT);

    TypioArenaBlock *block = arena->current;

    /* If the current block doesn't have enough space */
    if (block->capacity - block->used < size) {
        /* If the requested size is larger than default block size, create a custom sized block */
        size_t new_capacity = (size > arena->block_size) ? size : arena->block_size;
        
        TypioArenaBlock *new_block = allocate_block(new_capacity);
        if (!new_block) return NULL;
        
        /* Insert at the head of the block list */
        new_block->next = arena->current;
        arena->current = new_block;
        block = new_block;
    }

    void *ptr = block->data + block->used;
    block->used += size;
    return ptr;
}

char *typio_arena_strdup(TypioArena *arena, const char *str) {
    if (!arena || !str) return NULL;
    
    size_t len = strlen(str);
    char *copy = typio_arena_alloc(arena, len + 1);
    if (copy) {
        memcpy(copy, str, len + 1);
    }
    return copy;
}

void typio_arena_reset(TypioArena *arena) {
    if (!arena || !arena->current) return;

    /* 
     * To maximize reuse and avoid malloc/free churn, we don't free the blocks.
     * We just reset the 'used' counter. 
     * NOTE: To avoid unbounded memory growth if a huge block was allocated, 
     * a more advanced implementation might free oversized blocks. For typical 
     * session string usage, this simple reset is extremely fast and safe.
     */
    TypioArenaBlock *block = arena->current;
    while (block) {
        block->used = 0;
        /* memset(block->data, 0, block->capacity); Optional: zero out for safety */
        block = block->next;
    }
}

void typio_arena_destroy(TypioArena *arena) {
    if (!arena) return;
    
    TypioArenaBlock *block = arena->current;
    while (block) {
        TypioArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    
    arena->current = NULL;
}
