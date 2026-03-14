/**
 * @file path_expand.c
 * @brief Simple path expansion helpers for engine configuration
 */

#include "path_expand.h"
#include "utils/string.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool append_bytes(char **buffer, size_t *length, size_t *capacity,
                         const char *bytes, size_t bytes_len) {
    char *resized;
    size_t required;

    if (!buffer || !length || !capacity || !bytes) {
        return false;
    }

    required = *length + bytes_len + 1;
    if (required > *capacity) {
        size_t new_capacity = *capacity ? *capacity : 64;
        while (new_capacity < required) {
            new_capacity *= 2;
        }

        resized = realloc(*buffer, new_capacity);
        if (!resized) {
            return false;
        }

        *buffer = resized;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *length, bytes, bytes_len);
    *length += bytes_len;
    (*buffer)[*length] = '\0';
    return true;
}

static bool append_cstr(char **buffer, size_t *length, size_t *capacity,
                        const char *text) {
    return append_bytes(buffer, length, capacity, text, text ? strlen(text) : 0);
}

static bool is_var_start(char ch) {
    return isalpha((unsigned char)ch) || ch == '_';
}

static bool is_var_char(char ch) {
    return isalnum((unsigned char)ch) || ch == '_';
}

char *typio_rime_expand_path(const char *path) {
    char *expanded = nullptr;
    size_t length = 0;
    size_t capacity = 0;
    size_t i = 0;

    if (!path) {
        return nullptr;
    }

    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            if (!append_cstr(&expanded, &length, &capacity, home)) {
                free(expanded);
                return nullptr;
            }
            i = 1;
        }
    }

    while (path[i] != '\0') {
        if (path[i] != '$') {
            if (!append_bytes(&expanded, &length, &capacity, &path[i], 1)) {
                free(expanded);
                return nullptr;
            }
            ++i;
            continue;
        }

        size_t token_start = i;
        const char *env_value = nullptr;
        size_t name_start = 0;
        size_t name_len = 0;

        if (path[i + 1] == '{') {
            name_start = i + 2;
            while (path[name_start + name_len] != '\0' &&
                   path[name_start + name_len] != '}') {
                ++name_len;
            }
            if (path[name_start + name_len] == '}') {
                char *name = typio_strndup(path + name_start, name_len);
                if (!name) {
                    free(expanded);
                    return nullptr;
                }
                env_value = getenv(name);
                free(name);

                if (env_value && *env_value) {
                    if (!append_cstr(&expanded, &length, &capacity, env_value)) {
                        free(expanded);
                        return nullptr;
                    }
                } else if (!append_bytes(&expanded, &length, &capacity,
                                         path + token_start,
                                         (name_start + name_len + 1) - token_start)) {
                    free(expanded);
                    return nullptr;
                }

                i = name_start + name_len + 1;
                continue;
            }
        } else if (is_var_start(path[i + 1])) {
            name_start = i + 1;
            while (is_var_char(path[name_start + name_len])) {
                ++name_len;
            }

            char *name = typio_strndup(path + name_start, name_len);
            if (!name) {
                free(expanded);
                return nullptr;
            }
            env_value = getenv(name);
            free(name);

            if (env_value && *env_value) {
                if (!append_cstr(&expanded, &length, &capacity, env_value)) {
                    free(expanded);
                    return nullptr;
                }
            } else if (!append_bytes(&expanded, &length, &capacity,
                                     path + token_start,
                                     (name_start + name_len) - token_start)) {
                free(expanded);
                return nullptr;
            }

            i = name_start + name_len;
            continue;
        }

        if (!append_bytes(&expanded, &length, &capacity, &path[i], 1)) {
            free(expanded);
            return nullptr;
        }
        ++i;
    }

    if (!expanded) {
        expanded = typio_strdup(path);
    }

    return expanded;
}
