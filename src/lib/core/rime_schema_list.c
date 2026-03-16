/**
 * @file rime_schema_list.c
 * @brief Shared helpers for discovering available Rime schemas.
 */

#include "typio/rime_schema_list.h"
#include "../utils/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static char *rime_path_join(const char *base, const char *suffix) {
    size_t base_len;
    size_t suffix_len;
    bool need_slash;
    char *path;

    if (!base || !suffix) {
        return NULL;
    }

    base_len = strlen(base);
    suffix_len = strlen(suffix);
    need_slash = base_len > 0 && base[base_len - 1] != '/';
    path = calloc(base_len + suffix_len + (need_slash ? 2U : 1U), sizeof(char));
    if (!path) {
        return NULL;
    }

    snprintf(path, base_len + suffix_len + (need_slash ? 2U : 1U),
             need_slash ? "%s/%s" : "%s%s", base, suffix);
    return path;
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
        char *schema_marker = strstr(line, "- schema:");
        char *id;

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
        path = rime_path_join(list->user_data_dir, relative_path);
        list->schemas[i].name = rime_parse_schema_name(path);
        free(path);

        if (list->schemas[i].name) {
            continue;
        }

        snprintf(relative_path, sizeof(relative_path), "build/%s.schema.yaml",
                 list->schemas[i].id);
        path = rime_path_join(list->user_data_dir, relative_path);
        list->schemas[i].name = rime_parse_schema_name(path);
        free(path);
    }
}

bool typio_rime_schema_list_load(const TypioConfig *config,
                                 const char *default_data_dir,
                                 TypioRimeSchemaList *list) {
    char *path;

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
            list->user_data_dir = strdup(user_data_dir);
        }
    }

    if (!list->user_data_dir && default_data_dir) {
        list->user_data_dir = strdup(default_data_dir);
    }

    if (list->user_data_dir) {
        path = rime_path_join(list->user_data_dir, "default.custom.yaml");
        if (!rime_parse_schema_list(path, list)) {
            free(path);
            path = rime_path_join(list->user_data_dir, "build/default.yaml");
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
