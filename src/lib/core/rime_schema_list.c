/**
 * @file rime_schema_list.c
 * @brief Shared helpers for discovering available Rime schemas.
 */

#include "typio/rime_schema_list.h"
#include "../utils/log.h"
#include "../utils/string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool rime_append_bytes(char **buffer,
                              size_t *length,
                              size_t *capacity,
                              const char *bytes,
                              size_t bytes_len) {
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

static bool rime_append_cstr(char **buffer,
                             size_t *length,
                             size_t *capacity,
                             const char *text) {
    return rime_append_bytes(buffer, length, capacity, text, text ? strlen(text) : 0);
}

static char *rime_expand_path(const char *path) {
    char *expanded = NULL;
    size_t length = 0;
    size_t capacity = 0;
    size_t i = 0;

    if (!path) {
        return NULL;
    }

    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            if (!rime_append_cstr(&expanded, &length, &capacity, home)) {
                free(expanded);
                return NULL;
            }
            i = 1;
        }
    }

    while (path[i] != '\0') {
        if (path[i] == '$') {
            size_t token_start = i;
            size_t name_start;
            size_t name_len = 0;
            const char *env_value = NULL;

            if (path[i + 1] == '{') {
                name_start = i + 2;
                while (path[name_start + name_len] != '\0' &&
                       path[name_start + name_len] != '}') {
                    ++name_len;
                }

                if (path[name_start + name_len] == '}') {
                    char *name = strndup(path + name_start, name_len);
                    if (!name) {
                        free(expanded);
                        return NULL;
                    }
                    env_value = getenv(name);
                    free(name);

                    if (env_value && *env_value) {
                        if (!rime_append_cstr(&expanded, &length, &capacity, env_value)) {
                            free(expanded);
                            return NULL;
                        }
                    } else if (!rime_append_bytes(&expanded, &length, &capacity,
                                                  path + token_start,
                                                  (name_start + name_len + 1) - token_start)) {
                        free(expanded);
                        return NULL;
                    }

                    i = name_start + name_len + 1;
                    continue;
                }
            } else {
                name_start = i + 1;
                while ((path[name_start + name_len] >= 'A' &&
                        path[name_start + name_len] <= 'Z') ||
                       (path[name_start + name_len] >= 'a' &&
                        path[name_start + name_len] <= 'z') ||
                       (path[name_start + name_len] >= '0' &&
                        path[name_start + name_len] <= '9') ||
                       path[name_start + name_len] == '_') {
                    ++name_len;
                }

                if (name_len > 0) {
                    char *name = strndup(path + name_start, name_len);
                    if (!name) {
                        free(expanded);
                        return NULL;
                    }
                    env_value = getenv(name);
                    free(name);

                    if (env_value && *env_value) {
                        if (!rime_append_cstr(&expanded, &length, &capacity, env_value)) {
                            free(expanded);
                            return NULL;
                        }
                    } else if (!rime_append_bytes(&expanded, &length, &capacity,
                                                  path + token_start,
                                                  (name_start + name_len) - token_start)) {
                        free(expanded);
                        return NULL;
                    }

                    i = name_start + name_len;
                    continue;
                }
            }
        }

        if (!rime_append_bytes(&expanded, &length, &capacity, path + i, 1)) {
            free(expanded);
            return NULL;
        }
        ++i;
    }

    if (!expanded) {
        expanded = strdup(path);
    }
    return expanded;
}

static char *rime_dup_trimmed_value(const char *text) {
    const char *start;
    const char *end;
    char *copy;

    if (!text) {
        return NULL;
    }

    start = text;
    while (*start == ' ' || *start == '\t' || *start == '"' || *start == '\'') {
        ++start;
    }

    end = start + strlen(start);
    while (end > start &&
           (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' ||
            end[-1] == '\t' || end[-1] == '"' || end[-1] == '\'')) {
        --end;
    }

    copy = calloc((size_t)(end - start) + 1U, sizeof(char));
    if (!copy) {
        return NULL;
    }

    memcpy(copy, start, (size_t)(end - start));
    return copy;
}

static bool rime_parse_schema_list(const char *path, TypioRimeSchemaList *list) {
    FILE *file;
    char line[512];

    if (!path || !list) {
        return false;
    }

    typio_log_debug("schema probe list path=%s", path);
    file = fopen(path, "r");
    if (!file) {
        typio_log_debug("schema list unreadable path=%s", path);
        return false;
    }

    while (fgets(line, sizeof(line), file) &&
           list->schema_count < TYPIO_RIME_SCHEMA_LIST_MAX_SCHEMAS) {
        const char *p = line;
        char *schema_marker;
        char *id;

        /* Skip leading whitespace and check for YAML comment */
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (*p == '#') {
            continue;
        }

        schema_marker = strstr(line, "- schema:");
        if (!schema_marker) {
            continue;
        }

        id = rime_dup_trimmed_value(schema_marker + 9);
        if (!id || !*id) {
            free(id);
            continue;
        }

        list->schemas[list->schema_count].id = id;
        list->schema_count++;
    }

    fclose(file);
    typio_log_debug("schema list loaded count=%zu path=%s",
                    list->schema_count, path);
    return list->schema_count > 0;
}

static char *rime_parse_schema_name(const char *path) {
    FILE *file;
    char line[512];
    bool in_schema = false;

    if (!path) {
        return NULL;
    }

    typio_log_debug("schema probe name path=%s", path);
    file = fopen(path, "r");
    if (!file) {
        return NULL;
    }

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "schema:", 7) == 0) {
            in_schema = true;
            continue;
        }

        if (in_schema && strncmp(line, "  name:", 7) == 0) {
            fclose(file);
            return rime_dup_trimmed_value(line + 7);
        }
    }

    fclose(file);
    return NULL;
}

static void rime_fill_schema_names(TypioRimeSchemaList *list) {
    size_t i;

    if (!list || !list->user_data_dir) {
        return;
    }

    for (i = 0; i < list->schema_count; ++i) {
        char relative_path[256];
        char *path;

        snprintf(relative_path, sizeof(relative_path), "%s.schema.yaml",
                 list->schemas[i].id);
        path = typio_path_join(list->user_data_dir, relative_path);
        list->schemas[i].name = rime_parse_schema_name(path);
        free(path);

        if (list->schemas[i].name) {
            continue;
        }

        snprintf(relative_path, sizeof(relative_path), "build/%s.schema.yaml",
                 list->schemas[i].id);
        path = typio_path_join(list->user_data_dir, relative_path);
        list->schemas[i].name = rime_parse_schema_name(path);
        free(path);
    }
}

bool typio_rime_schema_list_load(const TypioConfig *config,
                                 const char *default_data_dir,
                                 TypioRimeSchemaList *list) {
    char *path;
    char *expanded_default_dir = NULL;

    if (!list) {
        return false;
    }

    memset(list, 0, sizeof(*list));

    if (config) {
        const char *schema = typio_config_get_string(config, "schema", NULL);
        const char *user_data_dir =
            typio_config_get_string(config, "user_data_dir", NULL);

        if (schema && *schema) {
            list->current_schema = strdup(schema);
        }
        if (user_data_dir && *user_data_dir) {
            list->user_data_dir = rime_expand_path(user_data_dir);
        }
    }

    if (!list->user_data_dir && default_data_dir) {
        expanded_default_dir = rime_expand_path(default_data_dir);
        list->user_data_dir = expanded_default_dir;
    }

    if (list->user_data_dir) {
        path = typio_path_join(list->user_data_dir, "default.custom.yaml");
        if (!rime_parse_schema_list(path, list)) {
            free(path);
            path = typio_path_join(list->user_data_dir, "build/default.yaml");
            rime_parse_schema_list(path, list);
        }
        free(path);
    }

    rime_fill_schema_names(list);
    typio_log_debug("schema resolved user_dir=%s current=%s count=%zu",
                    list->user_data_dir ? list->user_data_dir : "(null)",
                    list->current_schema ? list->current_schema : "(null)",
                    list->schema_count);
    list->available = list->current_schema != NULL || list->schema_count > 0;
    return list->available;
}

void typio_rime_schema_list_clear(TypioRimeSchemaList *list) {
    size_t i;

    if (!list) {
        return;
    }

    free(list->current_schema);
    free(list->user_data_dir);
    for (i = 0; i < list->schema_count; ++i) {
        free(list->schemas[i].id);
        free(list->schemas[i].name);
    }

    memset(list, 0, sizeof(*list));
}
