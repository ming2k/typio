/**
 * @file config.c
 * @brief Configuration management implementation (INI format)
 */

#define _POSIX_C_SOURCE 200809L

#include "typio/config.h"
#include "../utils/string.h"
#include "../utils/log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

/* Configuration entry */
typedef struct ConfigEntry {
    char *key;
    TypioConfigValue value;
    struct ConfigEntry *next;
} ConfigEntry;

struct TypioConfig {
    ConfigEntry *entries;
    size_t count;
};

static void free_config_value(TypioConfigValue *value) {
    if (!value) {
        return;
    }

    switch (value->type) {
        case TYPIO_CONFIG_STRING:
            free(value->data.string_val);
            break;
        case TYPIO_CONFIG_ARRAY:
            for (size_t i = 0; i < value->data.array_val.count; i++) {
                free_config_value(&value->data.array_val.items[i]);
            }
            free(value->data.array_val.items);
            break;
        case TYPIO_CONFIG_OBJECT:
            typio_config_free(value->data.object_val);
            break;
        default:
            break;
    }
}

static void free_entry(ConfigEntry *entry) {
    if (!entry) {
        return;
    }
    free(entry->key);
    free_config_value(&entry->value);
    free(entry);
}

TypioConfig *typio_config_new(void) {
    return calloc(1, sizeof(TypioConfig));
}

static char *trim_whitespace(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

TypioConfig *typio_config_load_file(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        return NULL;
    }

    TypioConfig *config = typio_config_new();
    if (!config) {
        fclose(file);
        return NULL;
    }

    char line[1024];
    char current_section[256] = "";

    while (fgets(line, sizeof(line), file)) {
        char *trimmed = trim_whitespace(line);

        /* Skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';') {
            continue;
        }

        /* Section header */
        if (*trimmed == '[') {
            char *end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                strncpy(current_section, trimmed + 1, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
            }
            continue;
        }

        /* Key-value pair */
        char *eq = strchr(trimmed, '=');
        if (eq) {
            *eq = '\0';
            char *key = trim_whitespace(trimmed);
            char *value = trim_whitespace(eq + 1);

            /* Build full key with section prefix */
            char full_key[512];
            if (current_section[0]) {
                snprintf(full_key, sizeof(full_key), "%s.%s", current_section, key);
            } else {
                strncpy(full_key, key, sizeof(full_key) - 1);
                full_key[sizeof(full_key) - 1] = '\0';
            }

            /* Detect value type */
            if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
                typio_config_set_bool(config, full_key, strcmp(value, "true") == 0);
            } else if (*value == '-' || isdigit((unsigned char)*value)) {
                char *end;
                errno = 0;
                if (strchr(value, '.')) {
                    double fval = strtod(value, &end);
                    if (end != value && *end == '\0' && errno == 0) {
                        typio_config_set_float(config, full_key, fval);
                    } else {
                        typio_config_set_string(config, full_key, value);
                    }
                } else {
                    long ival = strtol(value, &end, 10);
                    if (end != value && *end == '\0' && errno == 0 &&
                        ival >= INT_MIN && ival <= INT_MAX) {
                        typio_config_set_int(config, full_key, (int)ival);
                    } else {
                        typio_config_set_string(config, full_key, value);
                    }
                }
            } else {
                /* String value - remove quotes if present */
                size_t len = strlen(value);
                if (len >= 2 && ((value[0] == '"' && value[len-1] == '"') ||
                                 (value[0] == '\'' && value[len-1] == '\''))) {
                    value[len-1] = '\0';
                    value++;
                }
                typio_config_set_string(config, full_key, value);
            }
        }
    }

    fclose(file);
    return config;
}

TypioConfig *typio_config_load_string(const char *content) {
    if (!content) {
        return NULL;
    }

    TypioConfig *config = typio_config_new();
    if (!config) {
        return NULL;
    }

    /* Work on a mutable copy so we can tokenize in-place */
    char *buf = typio_strdup(content);
    if (!buf) {
        typio_config_free(config);
        return NULL;
    }

    char current_section[256] = "";
    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);

    while (line) {
        char *trimmed = line;
        while (isspace((unsigned char)*trimmed)) trimmed++;
        if (*trimmed == '\0') { trimmed = line; }

        /* Remove trailing whitespace */
        char *end = trimmed + strlen(trimmed) - 1;
        while (end > trimmed && isspace((unsigned char)*end)) {
            *end = '\0';
            end--;
        }

        /* Skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';') {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Section header */
        if (*trimmed == '[') {
            char *close = strchr(trimmed, ']');
            if (close) {
                *close = '\0';
                strncpy(current_section, trimmed + 1, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
            }
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Key-value pair */
        char *eq = strchr(trimmed, '=');
        if (eq) {
            *eq = '\0';
            char *key = trimmed;
            /* Trim key trailing whitespace */
            char *kend = key + strlen(key) - 1;
            while (kend > key && isspace((unsigned char)*kend)) {
                *kend = '\0';
                kend--;
            }
            /* Trim value leading whitespace */
            char *value = eq + 1;
            while (isspace((unsigned char)*value)) value++;

            char full_key[512];
            if (current_section[0]) {
                snprintf(full_key, sizeof(full_key), "%s.%s", current_section, key);
            } else {
                strncpy(full_key, key, sizeof(full_key) - 1);
                full_key[sizeof(full_key) - 1] = '\0';
            }

            /* Detect value type */
            if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
                typio_config_set_bool(config, full_key, strcmp(value, "true") == 0);
            } else if (*value == '-' || isdigit((unsigned char)*value)) {
                char *nend;
                errno = 0;
                if (strchr(value, '.')) {
                    double fval = strtod(value, &nend);
                    if (nend != value && *nend == '\0' && errno == 0) {
                        typio_config_set_float(config, full_key, fval);
                    } else {
                        typio_config_set_string(config, full_key, value);
                    }
                } else {
                    long ival = strtol(value, &nend, 10);
                    if (nend != value && *nend == '\0' && errno == 0 &&
                        ival >= INT_MIN && ival <= INT_MAX) {
                        typio_config_set_int(config, full_key, (int)ival);
                    } else {
                        typio_config_set_string(config, full_key, value);
                    }
                }
            } else {
                size_t len = strlen(value);
                if (len >= 2 && ((value[0] == '"' && value[len-1] == '"') ||
                                 (value[0] == '\'' && value[len-1] == '\''))) {
                    value[len-1] = '\0';
                    value++;
                }
                typio_config_set_string(config, full_key, value);
            }
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(buf);
    return config;
}

void typio_config_free(TypioConfig *config) {
    if (!config) {
        return;
    }

    ConfigEntry *entry = config->entries;
    while (entry) {
        ConfigEntry *next = entry->next;
        free_entry(entry);
        entry = next;
    }

    free(config);
}

TypioResult typio_config_save_file(const TypioConfig *config, const char *path) {
    if (!config || !path) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    FILE *file = fopen(path, "w");
    if (!file) {
        return TYPIO_ERROR;
    }

    fprintf(file, "# Typio configuration file\n\n");

    /* Group entries by section */
    char current_section[256] = "";

    ConfigEntry *entry = config->entries;
    while (entry) {
        /* Check for section prefix */
        const char *dot = strchr(entry->key, '.');
        if (dot) {
            char section[256];
            size_t section_len = (size_t)(dot - entry->key);
            if (section_len >= sizeof(section)) {
                section_len = sizeof(section) - 1;
            }
            memcpy(section, entry->key, section_len);
            section[section_len] = '\0';

            if (strcmp(section, current_section) != 0) {
                strncpy(current_section, section, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
                fprintf(file, "\n[%s]\n", current_section);
            }

            /* Write key without section prefix */
            const char *key = dot + 1;
            switch (entry->value.type) {
                case TYPIO_CONFIG_STRING:
                    fprintf(file, "%s = %s\n", key, entry->value.data.string_val);
                    break;
                case TYPIO_CONFIG_INT:
                    fprintf(file, "%s = %d\n", key, entry->value.data.int_val);
                    break;
                case TYPIO_CONFIG_BOOL:
                    fprintf(file, "%s = %s\n", key,
                            entry->value.data.bool_val ? "true" : "false");
                    break;
                case TYPIO_CONFIG_FLOAT:
                    fprintf(file, "%s = %g\n", key, entry->value.data.float_val);
                    break;
                default:
                    break;
            }
        } else {
            /* No section - write at top level */
            if (current_section[0] != '\0') {
                current_section[0] = '\0';
                fprintf(file, "\n");
            }

            switch (entry->value.type) {
                case TYPIO_CONFIG_STRING:
                    fprintf(file, "%s = %s\n", entry->key,
                            entry->value.data.string_val);
                    break;
                case TYPIO_CONFIG_INT:
                    fprintf(file, "%s = %d\n", entry->key,
                            entry->value.data.int_val);
                    break;
                case TYPIO_CONFIG_BOOL:
                    fprintf(file, "%s = %s\n", entry->key,
                            entry->value.data.bool_val ? "true" : "false");
                    break;
                case TYPIO_CONFIG_FLOAT:
                    fprintf(file, "%s = %g\n", entry->key,
                            entry->value.data.float_val);
                    break;
                default:
                    break;
            }
        }

        entry = entry->next;
    }

    fclose(file);
    return TYPIO_OK;
}

char *typio_config_to_string(const TypioConfig *config) {
    char *content = NULL;
    size_t content_len = 0;

    if (!config) {
        return NULL;
    }

    FILE *stream = open_memstream(&content, &content_len);
    if (!stream) {
        return NULL;
    }

    fprintf(stream, "# Typio configuration file\n\n");

    char current_section[256] = "";
    ConfigEntry *entry = config->entries;
    while (entry) {
        const char *dot = strchr(entry->key, '.');
        const char *write_key = entry->key;

        if (dot) {
            char section[256];
            size_t section_len = (size_t)(dot - entry->key);
            if (section_len >= sizeof(section)) {
                section_len = sizeof(section) - 1;
            }
            memcpy(section, entry->key, section_len);
            section[section_len] = '\0';

            if (strcmp(section, current_section) != 0) {
                strncpy(current_section, section, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
                fprintf(stream, "\n[%s]\n", current_section);
            }
            write_key = dot + 1;
        } else {
            if (current_section[0] != '\0') {
                current_section[0] = '\0';
                fprintf(stream, "\n");
            }
        }

        switch (entry->value.type) {
            case TYPIO_CONFIG_STRING:
                fprintf(stream, "%s = %s\n", write_key, entry->value.data.string_val);
                break;
            case TYPIO_CONFIG_INT:
                fprintf(stream, "%s = %d\n", write_key, entry->value.data.int_val);
                break;
            case TYPIO_CONFIG_BOOL:
                fprintf(stream, "%s = %s\n", write_key,
                        entry->value.data.bool_val ? "true" : "false");
                break;
            case TYPIO_CONFIG_FLOAT:
                fprintf(stream, "%s = %g\n", write_key, entry->value.data.float_val);
                break;
            default:
                break;
        }

        entry = entry->next;
    }

    fclose(stream);
    return content;
}

static ConfigEntry *find_entry(const TypioConfig *config, const char *key) {
    ConfigEntry *entry = config->entries;
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

const char *typio_config_get_string(const TypioConfig *config, const char *key,
                                     const char *default_val) {
    if (!config || !key) {
        return default_val;
    }

    ConfigEntry *entry = find_entry(config, key);
    if (entry && entry->value.type == TYPIO_CONFIG_STRING) {
        return entry->value.data.string_val;
    }

    return default_val;
}

int typio_config_get_int(const TypioConfig *config, const char *key,
                          int default_val) {
    if (!config || !key) {
        return default_val;
    }

    ConfigEntry *entry = find_entry(config, key);
    if (entry && entry->value.type == TYPIO_CONFIG_INT) {
        return entry->value.data.int_val;
    }

    return default_val;
}

bool typio_config_get_bool(const TypioConfig *config, const char *key,
                            bool default_val) {
    if (!config || !key) {
        return default_val;
    }

    ConfigEntry *entry = find_entry(config, key);
    if (entry && entry->value.type == TYPIO_CONFIG_BOOL) {
        return entry->value.data.bool_val;
    }

    return default_val;
}

double typio_config_get_float(const TypioConfig *config, const char *key,
                               double default_val) {
    if (!config || !key) {
        return default_val;
    }

    ConfigEntry *entry = find_entry(config, key);
    if (entry && entry->value.type == TYPIO_CONFIG_FLOAT) {
        return entry->value.data.float_val;
    }

    return default_val;
}

const TypioConfigValue *typio_config_get(const TypioConfig *config,
                                          const char *key) {
    if (!config || !key) {
        return NULL;
    }

    ConfigEntry *entry = find_entry(config, key);
    return entry ? &entry->value : NULL;
}

static TypioResult set_value(TypioConfig *config, const char *key,
                              const TypioConfigValue *value) {
    if (!config || !key) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    /* Check if entry exists */
    ConfigEntry *entry = find_entry(config, key);
    if (entry) {
        /* Free old value */
        free_config_value(&entry->value);
        entry->value = *value;
        return TYPIO_OK;
    }

    /* Create new entry */
    entry = calloc(1, sizeof(ConfigEntry));
    if (!entry) {
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    entry->key = typio_strdup(key);
    entry->value = *value;
    entry->next = config->entries;
    config->entries = entry;
    config->count++;

    return TYPIO_OK;
}

TypioResult typio_config_set_string(TypioConfig *config, const char *key,
                                     const char *value) {
    TypioConfigValue val = {
        .type = TYPIO_CONFIG_STRING,
        .data.string_val = typio_strdup(value)
    };
    return set_value(config, key, &val);
}

TypioResult typio_config_set_int(TypioConfig *config, const char *key,
                                  int value) {
    TypioConfigValue val = {
        .type = TYPIO_CONFIG_INT,
        .data.int_val = value
    };
    return set_value(config, key, &val);
}

TypioResult typio_config_set_bool(TypioConfig *config, const char *key,
                                   bool value) {
    TypioConfigValue val = {
        .type = TYPIO_CONFIG_BOOL,
        .data.bool_val = value
    };
    return set_value(config, key, &val);
}

TypioResult typio_config_set_float(TypioConfig *config, const char *key,
                                    double value) {
    TypioConfigValue val = {
        .type = TYPIO_CONFIG_FLOAT,
        .data.float_val = value
    };
    return set_value(config, key, &val);
}

TypioConfig *typio_config_get_section(const TypioConfig *config,
                                       const char *section) {
    if (!config || !section) {
        return NULL;
    }

    TypioConfig *sub = typio_config_new();
    if (!sub) {
        return NULL;
    }

    char prefix[256];
    snprintf(prefix, sizeof(prefix), "%s.", section);
    size_t prefix_len = strlen(prefix);

    ConfigEntry *entry = config->entries;
    while (entry) {
        if (strncmp(entry->key, prefix, prefix_len) == 0) {
            const char *subkey = entry->key + prefix_len;
            switch (entry->value.type) {
                case TYPIO_CONFIG_STRING:
                    typio_config_set_string(sub, subkey,
                                            entry->value.data.string_val);
                    break;
                case TYPIO_CONFIG_INT:
                    typio_config_set_int(sub, subkey,
                                         entry->value.data.int_val);
                    break;
                case TYPIO_CONFIG_BOOL:
                    typio_config_set_bool(sub, subkey,
                                          entry->value.data.bool_val);
                    break;
                case TYPIO_CONFIG_FLOAT:
                    typio_config_set_float(sub, subkey,
                                           entry->value.data.float_val);
                    break;
                default:
                    break;
            }
        }
        entry = entry->next;
    }

    return sub;
}

TypioResult typio_config_set_section(TypioConfig *config, const char *section,
                                      TypioConfig *sub_config) {
    if (!config || !section || !sub_config) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    char prefix[256];

    ConfigEntry *entry = sub_config->entries;
    while (entry) {
        snprintf(prefix, sizeof(prefix), "%s.%s", section, entry->key);

        switch (entry->value.type) {
            case TYPIO_CONFIG_STRING:
                typio_config_set_string(config, prefix,
                                        entry->value.data.string_val);
                break;
            case TYPIO_CONFIG_INT:
                typio_config_set_int(config, prefix,
                                     entry->value.data.int_val);
                break;
            case TYPIO_CONFIG_BOOL:
                typio_config_set_bool(config, prefix,
                                      entry->value.data.bool_val);
                break;
            case TYPIO_CONFIG_FLOAT:
                typio_config_set_float(config, prefix,
                                       entry->value.data.float_val);
                break;
            default:
                break;
        }
        entry = entry->next;
    }

    return TYPIO_OK;
}

size_t typio_config_get_array_size(const TypioConfig *config, const char *key) {
    if (!config || !key) {
        return 0;
    }

    ConfigEntry *entry = find_entry(config, key);
    if (entry && entry->value.type == TYPIO_CONFIG_ARRAY) {
        return entry->value.data.array_val.count;
    }

    return 0;
}

const char *typio_config_get_array_string(const TypioConfig *config,
                                           const char *key, size_t index) {
    if (!config || !key) {
        return NULL;
    }

    ConfigEntry *entry = find_entry(config, key);
    if (entry && entry->value.type == TYPIO_CONFIG_ARRAY &&
        index < entry->value.data.array_val.count) {
        TypioConfigValue *item = &entry->value.data.array_val.items[index];
        if (item->type == TYPIO_CONFIG_STRING) {
            return item->data.string_val;
        }
    }

    return NULL;
}

int typio_config_get_array_int(const TypioConfig *config,
                                const char *key, size_t index) {
    if (!config || !key) {
        return 0;
    }

    ConfigEntry *entry = find_entry(config, key);
    if (entry && entry->value.type == TYPIO_CONFIG_ARRAY &&
        index < entry->value.data.array_val.count) {
        TypioConfigValue *item = &entry->value.data.array_val.items[index];
        if (item->type == TYPIO_CONFIG_INT) {
            return item->data.int_val;
        }
    }

    return 0;
}

size_t typio_config_key_count(const TypioConfig *config) {
    return config ? config->count : 0;
}

const char *typio_config_key_at(const TypioConfig *config, size_t index) {
    if (!config || index >= config->count) {
        return NULL;
    }

    ConfigEntry *entry = config->entries;
    for (size_t i = 0; i < index && entry; i++) {
        entry = entry->next;
    }

    return entry ? entry->key : NULL;
}

bool typio_config_has_key(const TypioConfig *config, const char *key) {
    return find_entry(config, key) != NULL;
}

TypioResult typio_config_remove(TypioConfig *config, const char *key) {
    if (!config || !key) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    ConfigEntry **prev = &config->entries;
    ConfigEntry *entry = config->entries;

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            *prev = entry->next;
            free_entry(entry);
            config->count--;
            return TYPIO_OK;
        }
        prev = &entry->next;
        entry = entry->next;
    }

    return TYPIO_ERROR_NOT_FOUND;
}

TypioResult typio_config_merge(TypioConfig *dest, const TypioConfig *src) {
    if (!dest || !src) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    ConfigEntry *entry = src->entries;
    while (entry) {
        switch (entry->value.type) {
            case TYPIO_CONFIG_STRING:
                typio_config_set_string(dest, entry->key,
                                        entry->value.data.string_val);
                break;
            case TYPIO_CONFIG_INT:
                typio_config_set_int(dest, entry->key,
                                     entry->value.data.int_val);
                break;
            case TYPIO_CONFIG_BOOL:
                typio_config_set_bool(dest, entry->key,
                                      entry->value.data.bool_val);
                break;
            case TYPIO_CONFIG_FLOAT:
                typio_config_set_float(dest, entry->key,
                                       entry->value.data.float_val);
                break;
            default:
                break;
        }
        entry = entry->next;
    }

    return TYPIO_OK;
}
