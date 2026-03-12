/**
 * @file list.h
 * @brief Simple linked list utilities
 */

#ifndef TYPIO_UTILS_LIST_H
#define TYPIO_UTILS_LIST_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Generic list node */
typedef struct TypioListNode {
    void *data;
    struct TypioListNode *next;
    struct TypioListNode *prev;
} TypioListNode;

/* Generic list */
typedef struct TypioList {
    TypioListNode *head;
    TypioListNode *tail;
    size_t count;
    void (*free_func)(void *);
} TypioList;

/* List operations */
TypioList *typio_list_new(void (*free_func)(void *));
void typio_list_free(TypioList *list);

void typio_list_append(TypioList *list, void *data);
void typio_list_prepend(TypioList *list, void *data);
void typio_list_insert_at(TypioList *list, size_t index, void *data);

void *typio_list_get(TypioList *list, size_t index);
void *typio_list_remove(TypioList *list, size_t index);
void *typio_list_remove_data(TypioList *list, void *data);
void typio_list_clear(TypioList *list);

size_t typio_list_count(TypioList *list);
bool typio_list_is_empty(TypioList *list);

/* Iteration */
typedef void (*TypioListCallback)(void *data, void *user_data);
void typio_list_foreach(TypioList *list, TypioListCallback callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_UTILS_LIST_H */
