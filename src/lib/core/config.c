/**
 * @file config.c
 * @brief Configuration management implementation (INI format)
 */


#include "typio/config.h"
#include "../utils/string.h"
#include "../utils/log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>

#define TYPIO_CONFIG_HEADER "# Typio configuration file (TOML-compatible subset)\n\n"

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

static TypioResult set_value(TypioConfig *config, const char *key,
                             const TypioConfigValue *value);
static TypioResult parse_array_value(TypioConfig *config,
                                     const char *key,
                                     char *value);

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

static void write_escaped_string(FILE *file, const char *value) {
    const unsigned char *p = (const unsigned char *)(value ? value : "");

    fputc('"', file);
    while (*p) {
        switch (*p) {
        case '\\':
            fputs("\\\\", file);
            break;
        case '"':
            fputs("\\\"", file);
            break;
        case '\n':
            fputs("\\n", file);
            break;
        case '\r':
            fputs("\\r", file);
            break;
        case '\t':
            fputs("\\t", file);
            break;
        default:
            fputc((int)*p, file);
            break;
        }
        ++p;
    }
    fputc('"', file);
}

TypioConfig *typio_config_load_file(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        return nullptr;
    }

    TypioConfig *config = typio_config_new();
    if (!config) {
        fclose(file);
        return nullptr;
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
            if (*value == '[') {
                parse_array_value(config, full_key, value);
            } else if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
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
        return nullptr;
    }

    TypioConfig *config = typio_config_new();
    if (!config) {
        return nullptr;
    }

    /* Work on a mutable copy so we can tokenize in-place */
    char *buf = typio_strdup(content);
    if (!buf) {
        typio_config_free(config);
        return nullptr;
    }

    char current_section[256] = "";
    char *saveptr = nullptr;
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
            line = strtok_r(nullptr, "\n", &saveptr);
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
            line = strtok_r(nullptr, "\n", &saveptr);
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
            if (*value == '[') {
                parse_array_value(config, full_key, value);
            } else if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
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

        line = strtok_r(nullptr, "\n", &saveptr);
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

static void write_entry_value(FILE *stream, const char *key,
                              const ConfigEntry *entry) {
    size_t i;

    switch (entry->value.type) {
    case TYPIO_CONFIG_STRING:
        fprintf(stream, "%s = ", key);
        write_escaped_string(stream, entry->value.data.string_val);
        fputc('\n', stream);
        break;
    case TYPIO_CONFIG_INT:
        fprintf(stream, "%s = %d\n", key, entry->value.data.int_val);
        break;
    case TYPIO_CONFIG_BOOL:
        fprintf(stream, "%s = %s\n", key,
                entry->value.data.bool_val ? "true" : "false");
        break;
    case TYPIO_CONFIG_FLOAT:
        fprintf(stream, "%s = %g\n", key, entry->value.data.float_val);
        break;
    case TYPIO_CONFIG_ARRAY:
        fprintf(stream, "%s = [", key);
        for (i = 0; i < entry->value.data.array_val.count; ++i) {
            const TypioConfigValue *item = &entry->value.data.array_val.items[i];
            if (i > 0) {
                fputs(", ", stream);
            }
            switch (item->type) {
            case TYPIO_CONFIG_STRING:
                write_escaped_string(stream, item->data.string_val);
                break;
            case TYPIO_CONFIG_INT:
                fprintf(stream, "%d", item->data.int_val);
                break;
            case TYPIO_CONFIG_BOOL:
                fputs(item->data.bool_val ? "true" : "false", stream);
                break;
            case TYPIO_CONFIG_FLOAT:
                fprintf(stream, "%g", item->data.float_val);
                break;
            default:
                break;
            }
        }
        fputs("]\n", stream);
        break;
    default:
        break;
    }
}

static TypioResult parse_array_value(TypioConfig *config,
                                     const char *key,
                                     char *value) {
    TypioConfigValue array = {};
    char *cursor;
    size_t capacity = 0;

    if (!config || !key || !value) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    cursor = trim_whitespace(value);
    if (*cursor != '[') {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    cursor++;
    for (;;) {
        TypioConfigValue item = {};
        char *item_start;
        char *item_end;

        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }

        if (*cursor == ']') {
            cursor++;
            break;
        }
        if (*cursor == '\0') {
            free_config_value(&array);
            return TYPIO_ERROR;
        }

        item_start = cursor;
        if (*cursor == '"' || *cursor == '\'') {
            char quote = *cursor++;
            item_start = cursor;
            while (*cursor && *cursor != quote) {
                if (*cursor == '\\' && cursor[1] != '\0') {
                    cursor += 2;
                } else {
                    cursor++;
                }
            }
            if (*cursor != quote) {
                free_config_value(&array);
                return TYPIO_ERROR;
            }
            item_end = cursor;
            *cursor++ = '\0';
            item.type = TYPIO_CONFIG_STRING;
            item.data.string_val = typio_strdup(item_start);
        } else {
            char *nend;
            item_end = cursor;
            while (*item_end && *item_end != ',' && *item_end != ']') {
                item_end++;
            }
            if (*item_end != '\0') {
                char saved = *item_end;
                *item_end = '\0';
                item_start = trim_whitespace(item_start);
                if (strcmp(item_start, "true") == 0 || strcmp(item_start, "false") == 0) {
                    item.type = TYPIO_CONFIG_BOOL;
                    item.data.bool_val = strcmp(item_start, "true") == 0;
                } else if (*item_start == '-' || isdigit((unsigned char)*item_start)) {
                    errno = 0;
                    if (strchr(item_start, '.')) {
                        double fval = strtod(item_start, &nend);
                        if (nend != item_start && *nend == '\0' && errno == 0) {
                            item.type = TYPIO_CONFIG_FLOAT;
                            item.data.float_val = fval;
                        } else {
                            item.type = TYPIO_CONFIG_STRING;
                            item.data.string_val = typio_strdup(item_start);
                        }
                    } else {
                        long ival = strtol(item_start, &nend, 10);
                        if (nend != item_start && *nend == '\0' && errno == 0 &&
                            ival >= INT_MIN && ival <= INT_MAX) {
                            item.type = TYPIO_CONFIG_INT;
                            item.data.int_val = (int)ival;
                        } else {
                            item.type = TYPIO_CONFIG_STRING;
                            item.data.string_val = typio_strdup(item_start);
                        }
                    }
                } else {
                    item.type = TYPIO_CONFIG_STRING;
                    item.data.string_val = typio_strdup(item_start);
                }
                *item_end = saved;
                cursor = item_end;
            }
        }

        if (array.data.array_val.count == capacity) {
            size_t new_capacity = capacity == 0 ? 4 : capacity * 2;
            TypioConfigValue *items = realloc(array.data.array_val.items,
                                              new_capacity * sizeof(TypioConfigValue));
            if (!items) {
                free_config_value(&item);
                free_config_value(&array);
                return TYPIO_ERROR_OUT_OF_MEMORY;
            }
            array.data.array_val.items = items;
            capacity = new_capacity;
        }

        array.type = TYPIO_CONFIG_ARRAY;
        array.data.array_val.items[array.data.array_val.count++] = item;

        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor == ']') {
            cursor++;
            break;
        }
        if (*cursor == '\0') {
            free_config_value(&array);
            return TYPIO_ERROR;
        }
    }

    return set_value(config, key, &array);
}

/**
 * Extract the section prefix from a dotted key (e.g. "engines" from "engines.rime").
 * Returns empty string for top-level keys.  Writes into buf.
 */
static const char *entry_section(const ConfigEntry *entry,
                                 char *buf, size_t buf_size) {
    const char *dot = strchr(entry->key, '.');
    if (!dot) {
        buf[0] = '\0';
        return buf;
    }
    size_t len = (size_t)(dot - entry->key);
    if (len >= buf_size) {
        len = buf_size - 1;
    }
    memcpy(buf, entry->key, len);
    buf[len] = '\0';
    return buf;
}

TypioResult typio_config_save_file(const TypioConfig *config, const char *path) {
    ConfigEntry *e;
    char sections[64][256];
    size_t n_sections = 0;
    char tmp_path[PATH_MAX];
    const char *slash;
    int fd;
    FILE *file;

    if (!config || !path) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    if (strlen(path) + sizeof(".tmp.XXXXXX") >= sizeof(tmp_path)) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.XXXXXX", path);
    fd = mkstemp(tmp_path);
    if (fd < 0) {
        return TYPIO_ERROR;
    }

    file = fdopen(fd, "w");
    if (!file) {
        close(fd);
        unlink(tmp_path);
        return TYPIO_ERROR;
    }

    fputs(TYPIO_CONFIG_HEADER, file);

    /* Pass 1: write all top-level keys (no dot) before any section header */
    for (e = config->entries; e; e = e->next) {
        if (!strchr(e->key, '.')) {
            write_entry_value(file, e->key, e);
        }
    }

    /* Collect unique section names in insertion order */
    for (e = config->entries; e; e = e->next) {
        char sec[256];
        entry_section(e, sec, sizeof(sec));
        if (sec[0] == '\0') {
            continue;
        }
        bool found = false;
        for (size_t i = 0; i < n_sections; i++) {
            if (strcmp(sections[i], sec) == 0) {
                found = true;
                break;
            }
        }
        if (!found && n_sections < 64) {
            strncpy(sections[n_sections], sec, 255);
            sections[n_sections][255] = '\0';
            n_sections++;
        }
    }

    /* Pass 2: write each section with all its entries grouped */
    for (size_t i = 0; i < n_sections; i++) {
        size_t prefix_len = strlen(sections[i]);
        fprintf(file, "\n[%s]\n", sections[i]);

        for (e = config->entries; e; e = e->next) {
            char sec[256];
            entry_section(e, sec, sizeof(sec));
            if (strcmp(sec, sections[i]) == 0) {
                const char *subkey = e->key + prefix_len + 1;
                write_entry_value(file, subkey, e);
            }
        }
    }

    if (fflush(file) != 0) {
        fclose(file);
        unlink(tmp_path);
        return TYPIO_ERROR;
    }

    if (fsync(fd) != 0) {
        fclose(file);
        unlink(tmp_path);
        return TYPIO_ERROR;
    }

    if (fclose(file) != 0) {
        unlink(tmp_path);
        return TYPIO_ERROR;
    }

    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        return TYPIO_ERROR;
    }

    slash = strrchr(path, '/');
    if (slash) {
        char dir_path[PATH_MAX];
        int dir_fd;
        size_t dir_len = (size_t)(slash - path);

        if (dir_len < sizeof(dir_path)) {
            memcpy(dir_path, path, dir_len);
            dir_path[dir_len] = '\0';
            dir_fd = open(dir_path, O_RDONLY);
            if (dir_fd >= 0) {
                fsync(dir_fd);
                close(dir_fd);
            }
        }
    }

    return TYPIO_OK;
}

char *typio_config_to_string(const TypioConfig *config) {
    char *content = nullptr;
    size_t content_len = 0;

    if (!config) {
        return nullptr;
    }

    FILE *stream = open_memstream(&content, &content_len);
    if (!stream) {
        return nullptr;
    }

    fputs(TYPIO_CONFIG_HEADER, stream);

    /* Collect unique section names in order of first appearance */
    char sections[64][256];
    size_t n_sections = 0;

    /* First pass: collect top-level keys (no section) */
    for (ConfigEntry *e = config->entries; e; e = e->next) {
        if (!strchr(e->key, '.')) {
            write_entry_value(stream, e->key, e);
        }
    }

    /* Collect unique sections */
    for (ConfigEntry *e = config->entries; e; e = e->next) {
        char sec[256];
        entry_section(e, sec, sizeof(sec));
        if (sec[0] == '\0') {
            continue;
        }
        bool found = false;
        for (size_t i = 0; i < n_sections; i++) {
            if (strcmp(sections[i], sec) == 0) {
                found = true;
                break;
            }
        }
        if (!found && n_sections < 64) {
            strncpy(sections[n_sections], sec, 255);
            sections[n_sections][255] = '\0';
            n_sections++;
        }
    }

    /* Second pass: output each section with all its entries grouped */
    for (size_t i = 0; i < n_sections; i++) {
        fprintf(stream, "\n[%s]\n", sections[i]);
        size_t prefix_len = strlen(sections[i]);

        for (ConfigEntry *e = config->entries; e; e = e->next) {
            char sec[256];
            entry_section(e, sec, sizeof(sec));
            if (strcmp(sec, sections[i]) == 0) {
                const char *subkey = e->key + prefix_len + 1;
                write_entry_value(stream, subkey, e);
            }
        }
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
    return nullptr;
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
        return nullptr;
    }

    ConfigEntry *entry = find_entry(config, key);
    return entry ? &entry->value : nullptr;
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

TypioResult typio_config_set_string_array(TypioConfig *config, const char *key,
                                          const char *const *values, size_t count) {
    TypioConfigValue val = {};

    if (!config || !key || (!values && count > 0)) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    val.type = TYPIO_CONFIG_ARRAY;
    val.data.array_val.count = count;
    if (count == 0) {
        val.data.array_val.items = NULL;
        return set_value(config, key, &val);
    }

    val.data.array_val.items = calloc(count, sizeof(TypioConfigValue));
    if (!val.data.array_val.items) {
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < count; ++i) {
        val.data.array_val.items[i].type = TYPIO_CONFIG_STRING;
        val.data.array_val.items[i].data.string_val = typio_strdup(values[i] ? values[i] : "");
        if (!val.data.array_val.items[i].data.string_val) {
            free_config_value(&val);
            return TYPIO_ERROR_OUT_OF_MEMORY;
        }
    }

    return set_value(config, key, &val);
}

TypioConfig *typio_config_get_section(const TypioConfig *config,
                                       const char *section) {
    if (!config || !section) {
        return nullptr;
    }

    TypioConfig *sub = typio_config_new();
    if (!sub) {
        return nullptr;
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
            case TYPIO_CONFIG_ARRAY:
                if (entry->value.data.array_val.count == 0) {
                    typio_config_set_string_array(sub, subkey, NULL, 0);
                } else if (entry->value.data.array_val.items[0].type == TYPIO_CONFIG_STRING) {
                    const char **items = calloc(entry->value.data.array_val.count,
                                                sizeof(char *));
                    if (!items) {
                        typio_config_free(sub);
                        return nullptr;
                    }
                    for (size_t i = 0; i < entry->value.data.array_val.count; ++i) {
                        items[i] = entry->value.data.array_val.items[i].data.string_val;
                    }
                    typio_config_set_string_array(sub, subkey, items,
                                                  entry->value.data.array_val.count);
                    free(items);
                }
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
            case TYPIO_CONFIG_ARRAY:
                if (entry->value.data.array_val.count == 0) {
                    typio_config_set_string_array(config, prefix, NULL, 0);
                } else if (entry->value.data.array_val.items[0].type == TYPIO_CONFIG_STRING) {
                    const char **items = calloc(entry->value.data.array_val.count,
                                                sizeof(char *));
                    if (!items) {
                        return TYPIO_ERROR_OUT_OF_MEMORY;
                    }
                    for (size_t i = 0; i < entry->value.data.array_val.count; ++i) {
                        items[i] = entry->value.data.array_val.items[i].data.string_val;
                    }
                    typio_config_set_string_array(config, prefix, items,
                                                  entry->value.data.array_val.count);
                    free(items);
                }
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
        return nullptr;
    }

    ConfigEntry *entry = find_entry(config, key);
    if (entry && entry->value.type == TYPIO_CONFIG_ARRAY &&
        index < entry->value.data.array_val.count) {
        TypioConfigValue *item = &entry->value.data.array_val.items[index];
        if (item->type == TYPIO_CONFIG_STRING) {
            return item->data.string_val;
        }
    }

    return nullptr;
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
        return nullptr;
    }

    ConfigEntry *entry = config->entries;
    for (size_t i = 0; i < index && entry; i++) {
        entry = entry->next;
    }

    return entry ? entry->key : nullptr;
}

bool typio_config_has_key(const TypioConfig *config, const char *key) {
    return find_entry(config, key) != nullptr;
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
            case TYPIO_CONFIG_ARRAY:
                if (entry->value.data.array_val.count == 0) {
                    typio_config_set_string_array(dest, entry->key, NULL, 0);
                } else if (entry->value.data.array_val.items[0].type == TYPIO_CONFIG_STRING) {
                    const char **items = calloc(entry->value.data.array_val.count,
                                                sizeof(char *));
                    if (!items) {
                        return TYPIO_ERROR_OUT_OF_MEMORY;
                    }
                    for (size_t i = 0; i < entry->value.data.array_val.count; ++i) {
                        items[i] = entry->value.data.array_val.items[i].data.string_val;
                    }
                    typio_config_set_string_array(dest, entry->key, items,
                                                  entry->value.data.array_val.count);
                    free(items);
                }
                break;
            default:
                break;
        }
        entry = entry->next;
    }

    return TYPIO_OK;
}
