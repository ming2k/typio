/**
 * @file memory.h
 * @brief Ownership-aware allocation helpers for Typio core
 *
 * Typio uses explicit ownership tagging to avoid double-frees and use-after-free
 * bugs.  Every pointer that is owned by an object carries an ownership tag.
 */

#ifndef TYPIO_MEMORY_H
#define TYPIO_MEMORY_H

#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TYPIO_OWN_SELF,       /**< Allocated and freed by the containing object */
    TYPIO_OWN_PARENT,     /**< Owned by a parent object; do not free directly */
    TYPIO_OWN_EXTERNAL,   /**< Borrowed from outside; never free */
    TYPIO_OWN_TRANSFER,   /**< Ownership transferred to caller on return */
} TypioOwnership;

/**
 * @brief Free a pointer if it has self-ownership and zero the pointer.
 *
 * @param ptr       Address of the pointer to free.
 * @param owner     Current ownership tag.
 */
#define TYPIO_FREE_IF_OWNED(ptr, owner)                      \
    do {                                                     \
        if ((owner) == TYPIO_OWN_SELF && *(ptr) != nullptr) {\
            free(*(ptr));                                    \
            *(ptr) = nullptr;                                \
        }                                                    \
    } while (0)

/**
 * @brief Reassign a pointer, freeing the old value if it is self-owned.
 *
 * After the macro, @c ptr has ownership @c new_owner.
 */
#define TYPIO_ASSIGN(ptr, new_value, new_owner, old_owner)   \
    do {                                                     \
        TYPIO_FREE_IF_OWNED(&(ptr), old_owner);              \
        (ptr) = (new_value);                                 \
        (old_owner) = (new_owner);                           \
    } while (0)

/**
 * @brief Allocate and return a pointer with transfer ownership.
 *
 * The caller receives self-ownership of the returned pointer and must
 * free it when done.
 */
#define TYPIO_ALLOC(type, count, out_ptr)                    \
    do {                                                     \
        *(out_ptr) = (type *)calloc((count), sizeof(type));  \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_MEMORY_H */
