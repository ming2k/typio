/**
 * @file log.c
 * @brief Logging implementation
 */

#include "log.h"

#include "string.h"

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static TypioLogLevel g_log_level = TYPIO_LOG_INFO;
static TypioLogCallback g_log_callback = nullptr;
static void *g_log_user_data = nullptr;
static char g_recent_logs[TYPIO_LOG_RECENT_CAPACITY][1024];
static size_t g_recent_log_count = 0;
static size_t g_recent_log_start = 0;
static char *g_recent_dump_path = nullptr;

void typio_log_set_level(TypioLogLevel level) {
    g_log_level = level;
}

TypioLogLevel typio_log_get_level(void) {
    return g_log_level;
}

void typio_log_set_callback(TypioLogCallback callback, void *user_data) {
    g_log_callback = callback;
    g_log_user_data = user_data;
}

void typio_log_set_recent_dump_path(const char *path) {
    free(g_recent_dump_path);
    g_recent_dump_path = path ? strdup(path) : nullptr;
}

static const char *level_name(TypioLogLevel level) {
    switch (level) {
        case TYPIO_LOG_DEBUG:   return "DEBUG";
        case TYPIO_LOG_INFO:    return "INFO";
        case TYPIO_LOG_WARNING: return "WARN";
        case TYPIO_LOG_ERROR:   return "ERROR";
        default: return "UNKNOWN";
    }
}

static void store_recent_log_line(const char *line) {
    size_t index;

    if (!line)
        return;

    if (g_recent_log_count < TYPIO_LOG_RECENT_CAPACITY) {
        index = (g_recent_log_start + g_recent_log_count) % TYPIO_LOG_RECENT_CAPACITY;
        g_recent_log_count++;
    } else {
        index = g_recent_log_start;
        g_recent_log_start = (g_recent_log_start + 1) % TYPIO_LOG_RECENT_CAPACITY;
    }

    snprintf(g_recent_logs[index], sizeof(g_recent_logs[index]), "%s", line);
}

static bool ensure_directory_recursive(const char *path) {
    char *copy;
    size_t len;

    if (!path || !*path) {
        return false;
    }

    copy = typio_strdup(path);
    if (!copy) {
        return false;
    }

    len = strlen(copy);
    if (len > 1 && copy[len - 1] == '/') {
        copy[len - 1] = '\0';
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

    if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
        free(copy);
        return false;
    }

    free(copy);
    return true;
}

static bool ensure_parent_directory(const char *path) {
    char *parent;
    char *slash;
    bool ok;

    if (!path || !*path) {
        return false;
    }

    parent = typio_strdup(path);
    if (!parent) {
        return false;
    }

    slash = strrchr(parent, '/');
    if (!slash) {
        free(parent);
        return true;
    }

    if (slash == parent) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }

    ok = ensure_directory_recursive(parent);
    free(parent);
    return ok;
}

bool typio_log_dump_recent(const char *path) {
    FILE *fp;

    if (!path || !*path)
        return false;

    if (!ensure_parent_directory(path)) {
        return false;
    }

    fp = fopen(path, "w");
    if (!fp)
        return false;

    for (size_t i = 0; i < g_recent_log_count; ++i) {
        size_t index = (g_recent_log_start + i) % TYPIO_LOG_RECENT_CAPACITY;
        fprintf(fp, "%s\n", g_recent_logs[index]);
    }

    fclose(fp);
    return true;
}

bool typio_log_dump_recent_to_configured_path(void) {
    if (!g_recent_dump_path || !*g_recent_dump_path)
        return false;

    return typio_log_dump_recent(g_recent_dump_path);
}

void typio_log(TypioLogLevel level, const char *format, ...) {
    if (level < g_log_level) {
        return;
    }

    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    /* Default: render full line once so stderr and recent-log dump match. */
    time_t now = time(nullptr);
    struct tm *tm_info = localtime(&now);
    char time_buf[32];
    char rendered[1200];

    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    snprintf(rendered, sizeof(rendered), "[%s] [%s] %s",
             time_buf, level_name(level), message);
    store_recent_log_line(rendered);

    if (g_log_callback) {
        g_log_callback(level, message, g_log_user_data);
    } else {
        fprintf(stderr, "%s\n", rendered);
    }
}
