#ifndef INPUT_METHOD_UTILS_H
#define INPUT_METHOD_UTILS_H

#include <stddef.h>

// Memory management utilities
void* im_malloc(size_t size);
void* im_realloc(void* ptr, size_t size);
void im_free(void* ptr);

// String utilities
char* im_strdup(const char* str);
size_t im_utf8_strlen(const char* str);

#endif // INPUT_METHOD_UTILS_H 