/**
 * @file log.c
 * @brief Logging implementation
 */

#include "log.h"

#include "string.h"

#include <dirent.h>
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

#define TYPIO_LOG_ARCHIVE_RETENTION 20

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

static char *slugify_reason(const char *reason) {
    size_t len;
    char *slug;
    size_t out = 0;
    bool last_dash = false;

    if (!reason || !*reason) {
        return typio_strdup("snapshot");
    }

    len = strlen(reason);
    slug = calloc(len + 1, sizeof(char));
    if (!slug) {
        return nullptr;
    }

    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)reason[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            slug[out++] = (char)ch;
            last_dash = false;
        } else if (ch >= 'A' && ch <= 'Z') {
            slug[out++] = (char)(ch - 'A' + 'a');
            last_dash = false;
        } else if (!last_dash && out > 0) {
            slug[out++] = '-';
            last_dash = true;
        }
    }

    while (out > 0 && slug[out - 1] == '-') {
        out--;
    }
    slug[out] = '\0';

    if (out == 0) {
        free(slug);
        return typio_strdup("snapshot");
    }

    return slug;
}

static int archive_name_compare(const void *lhs, const void *rhs) {
    const char *const *a = lhs;
    const char *const *b = rhs;
    return strcmp(*a, *b);
}

static void prune_old_archives(const char *archive_dir) {
    DIR *dir;
    struct dirent *entry;
    char **names = nullptr;
    size_t count = 0;
    size_t capacity = 0;

    if (!archive_dir || !*archive_dir) {
        return;
    }

    dir = opendir(archive_dir);
    if (!dir) {
        return;
    }

    while ((entry = readdir(dir)) != nullptr) {
        char *name;
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (!typio_str_ends_with(entry->d_name, ".log")) {
            continue;
        }

        if (count == capacity) {
            size_t next_capacity = capacity == 0 ? 8 : capacity * 2;
            char **next = realloc(names, next_capacity * sizeof(char *));
            if (!next) {
                break;
            }
            names = next;
            capacity = next_capacity;
        }

        name = typio_strdup(entry->d_name);
        if (!name) {
            break;
        }
        names[count++] = name;
    }

    closedir(dir);

    if (count <= TYPIO_LOG_ARCHIVE_RETENTION) {
        for (size_t i = 0; i < count; ++i) {
            free(names[i]);
        }
        free(names);
        return;
    }

    qsort(names, count, sizeof(char *), archive_name_compare);

    for (size_t i = 0; i + TYPIO_LOG_ARCHIVE_RETENTION < count; ++i) {
        char *path = typio_path_join(archive_dir, names[i]);
        if (path) {
            unlink(path);
            free(path);
        }
    }

    for (size_t i = 0; i < count; ++i) {
        free(names[i]);
    }
    free(names);
}

static char *build_recent_archive_path_with_reason(const char *path,
                                                   const char *reason) {
    char *dir = nullptr;
    char *archive_dir = nullptr;
    char *reason_slug = nullptr;
    char *archive_name = nullptr;
    char *archive_path = nullptr;
    time_t now;
    struct tm tm_info;
    char stamp[64];

    if (!path || !*path) {
        return nullptr;
    }

    now = time(nullptr);
    if (localtime_r(&now, &tm_info) == nullptr) {
        return nullptr;
    }
    if (strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm_info) == 0) {
        return nullptr;
    }

    {
        const char *base_name = strrchr(path, '/');
        if (base_name) {
            dir = typio_strndup(path, (size_t)(base_name - path));
        } else {
            dir = typio_strdup(".");
        }
    }
    if (!dir) {
        return nullptr;
    }

    archive_dir = typio_path_join(dir, "archive");
    free(dir);
    if (!archive_dir) {
        return nullptr;
    }

    reason_slug = slugify_reason(reason);
    if (!reason_slug) {
        free(archive_dir);
        return nullptr;
    }

    archive_name = calloc(strlen(stamp) + strlen(reason_slug) + strlen(".log") + 2,
                          sizeof(char));
    if (!archive_name) {
        free(reason_slug);
        free(archive_dir);
        return nullptr;
    }

    snprintf(archive_name,
             strlen(stamp) + strlen(reason_slug) + strlen(".log") + 2,
             "%s_%s.log",
             stamp,
             reason_slug);

    archive_path = typio_path_join(archive_dir, archive_name);
    free(archive_name);
    free(reason_slug);
    free(archive_dir);
    return archive_path;
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

bool typio_log_dump_recent_to_configured_path(const char *reason) {
    bool ok;
    bool archive_ok = false;
    char *archive_path = nullptr;
    char *archive_dir = nullptr;

    if (!g_recent_dump_path || !*g_recent_dump_path)
        return false;

    ok = typio_log_dump_recent(g_recent_dump_path);
    if (ok) {
        archive_path = build_recent_archive_path_with_reason(g_recent_dump_path,
                                                             reason);
        if (archive_path) {
            archive_ok = typio_log_dump_recent(archive_path);
            if (archive_ok && strrchr(archive_path, '/')) {
                archive_dir = typio_strndup(
                    archive_path,
                    (size_t)(strrchr(archive_path, '/') - archive_path));
            }
            if (archive_ok && archive_dir) {
                prune_old_archives(archive_dir);
            }
        }
    }
    if (ok && reason && *reason) {
        if (archive_ok && archive_path) {
            fprintf(stderr,
                    "[typio] [INFO] Dumped recent logs to %s and %s (%s)\n",
                    g_recent_dump_path, archive_path, reason);
        } else {
            fprintf(stderr, "[typio] [INFO] Dumped recent logs to %s (%s)\n",
                    g_recent_dump_path, reason);
        }
    }
    free(archive_dir);
    free(archive_path);
    return ok;
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
