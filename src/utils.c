#include "input_method/utils.h"
#include <stdlib.h>
#include <string.h>

void* im_malloc(size_t size) {
    if (size == 0) return NULL;
    return malloc(size);
}

void* im_realloc(void* ptr, size_t size) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

void im_free(void* ptr) {
    free(ptr);
}

char* im_strdup(const char* str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char* new_str = im_malloc(len + 1);
    if (!new_str) return NULL;
    
    memcpy(new_str, str, len + 1);
    return new_str;
}

size_t im_utf8_strlen(const char* str) {
    if (!str) return 0;

    size_t len = 0;
    while (*str) {
        if ((*str & 0xC0) != 0x80) {
            len++;
        }
        str++;
    }
    return len;
} 