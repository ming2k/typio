/**
 * @file rime_utils.c
 * @brief Small utility helpers for the Rime engine
 */

#include "rime_internal.h"

uint64_t typio_rime_monotonic_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

bool typio_rime_ensure_dir(const char *path) {
    if (!path || !*path) {
        return false;
    }

    char *copy = typio_strdup(path);
    if (!copy) {
        return false;
    }

    for (char *p = copy + 1; *p; ++p) {
        if (*p != '/') {
            continue;
        }

        *p = '\0';
        if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
            free(copy);
            return false;
        }
        *p = '/';
    }

    const bool ok = mkdir(copy, 0755) == 0 || errno == EEXIST;
    free(copy);
    return ok;
}

bool typio_rime_path_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

bool typio_rime_has_yaml_suffix(const char *name) {
    const char *suffix;

    if (!name) {
        return false;
    }

    suffix = strrchr(name, '.');
    return suffix && strcmp(suffix, ".yaml") == 0;
}
