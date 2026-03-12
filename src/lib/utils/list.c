/**
 * @file list.c
 * @brief Linked list implementation
 */

#include "list.h"
#include <stdlib.h>

TypioList *typio_list_new(void (*free_func)(void *)) {
    TypioList *list = calloc(1, sizeof(TypioList));
    if (list) {
        list->free_func = free_func;
    }
    return list;
}

void typio_list_free(TypioList *list) {
    if (!list) {
        return;
    }

    typio_list_clear(list);
    free(list);
}

void typio_list_append(TypioList *list, void *data) {
    if (!list) {
        return;
    }

    TypioListNode *node = calloc(1, sizeof(TypioListNode));
    if (!node) {
        return;
    }

    node->data = data;

    if (list->tail) {
        list->tail->next = node;
        node->prev = list->tail;
        list->tail = node;
    } else {
        list->head = list->tail = node;
    }

    list->count++;
}

void typio_list_prepend(TypioList *list, void *data) {
    if (!list) {
        return;
    }

    TypioListNode *node = calloc(1, sizeof(TypioListNode));
    if (!node) {
        return;
    }

    node->data = data;

    if (list->head) {
        list->head->prev = node;
        node->next = list->head;
        list->head = node;
    } else {
        list->head = list->tail = node;
    }

    list->count++;
}

void typio_list_insert_at(TypioList *list, size_t index, void *data) {
    if (!list) {
        return;
    }

    if (index == 0) {
        typio_list_prepend(list, data);
        return;
    }

    if (index >= list->count) {
        typio_list_append(list, data);
        return;
    }

    TypioListNode *current = list->head;
    for (size_t i = 0; i < index; i++) {
        current = current->next;
    }

    TypioListNode *node = calloc(1, sizeof(TypioListNode));
    if (!node) {
        return;
    }

    node->data = data;
    node->prev = current->prev;
    node->next = current;

    if (current->prev) {
        current->prev->next = node;
    }
    current->prev = node;

    list->count++;
}

void *typio_list_get(TypioList *list, size_t index) {
    if (!list || index >= list->count) {
        return NULL;
    }

    TypioListNode *node = list->head;
    for (size_t i = 0; i < index; i++) {
        node = node->next;
    }

    return node->data;
}

void *typio_list_remove(TypioList *list, size_t index) {
    if (!list || index >= list->count) {
        return NULL;
    }

    TypioListNode *node = list->head;
    for (size_t i = 0; i < index; i++) {
        node = node->next;
    }

    if (node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }

    void *data = node->data;
    free(node);
    list->count--;

    return data;
}

void *typio_list_remove_data(TypioList *list, void *data) {
    if (!list) {
        return NULL;
    }

    TypioListNode *node = list->head;
    size_t index = 0;

    while (node) {
        if (node->data == data) {
            return typio_list_remove(list, index);
        }
        node = node->next;
        index++;
    }

    return NULL;
}

void typio_list_clear(TypioList *list) {
    if (!list) {
        return;
    }

    TypioListNode *node = list->head;
    while (node) {
        TypioListNode *next = node->next;
        if (list->free_func && node->data) {
            list->free_func(node->data);
        }
        free(node);
        node = next;
    }

    list->head = list->tail = NULL;
    list->count = 0;
}

size_t typio_list_count(TypioList *list) {
    return list ? list->count : 0;
}

bool typio_list_is_empty(TypioList *list) {
    return !list || list->count == 0;
}

void typio_list_foreach(TypioList *list, TypioListCallback callback, void *user_data) {
    if (!list || !callback) {
        return;
    }

    TypioListNode *node = list->head;
    while (node) {
        callback(node->data, user_data);
        node = node->next;
    }
}
