/**
 * @file arena.h
 * @brief Linear Arena Allocator for fast, bulk-releasable memory.
 *
 * An Arena allocates a large block of memory upfront. Subsequent allocations
 * simply bump a pointer, turning malloc/free overhead into O(1) pointer math.
 * Individual frees are not supported; the entire arena is reset at once.
 */

#ifndef TYPIO_ARENA_H
#define TYPIO_ARENA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioArenaBlock TypioArenaBlock;

typedef struct TypioArena {
    TypioArenaBlock *current;
    size_t block_size;
} TypioArena;

/**
 * @brief Initialize a new arena.
 * @param arena Pointer to the arena structure to initialize.
 * @param block_size The size of each memory block (e.g., 4096).
 * @return true if initialization succeeded, false on OOM.
 */
bool typio_arena_init(TypioArena *arena, size_t block_size);

/**
 * @brief Allocate memory from the arena.
 * @param arena The arena.
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on OOM. Memory is zeroed.
 */
void *typio_arena_alloc(TypioArena *arena, size_t size);

/**
 * @brief Duplicate a string using arena memory.
 * @param arena The arena.
 * @param str The null-terminated string to copy.
 * @return Pointer to the new string, or NULL on OOM or if str is NULL.
 */
char *typio_arena_strdup(TypioArena *arena, const char *str);

/**
 * @brief Reset the arena, invalidating all previously allocated memory.
 * This is extremely fast as it only resets block pointers.
 * @param arena The arena to reset.
 */
void typio_arena_reset(TypioArena *arena);

/**
 * @brief Free the arena and return all memory to the OS.
 * @param arena The arena to destroy.
 */
void typio_arena_destroy(TypioArena *arena);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_ARENA_H */
