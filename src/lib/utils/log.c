/**
 * @file log.c
 * @brief Logging implementation
 */

#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static TypioLogLevel g_log_level = TYPIO_LOG_INFO;
static TypioLogCallback g_log_callback = nullptr;
static void *g_log_user_data = nullptr;

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

static const char *level_name(TypioLogLevel level) {
    switch (level) {
        case TYPIO_LOG_DEBUG:   return "DEBUG";
        case TYPIO_LOG_INFO:    return "INFO";
        case TYPIO_LOG_WARNING: return "WARN";
        case TYPIO_LOG_ERROR:   return "ERROR";
        default: return "UNKNOWN";
    }
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

    if (g_log_callback) {
        g_log_callback(level, message, g_log_user_data);
    } else {
        /* Default: print to stderr */
        time_t now = time(nullptr);
        struct tm *tm_info = localtime(&now);
        char time_buf[32];
        strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);

        fprintf(stderr, "[%s] [%s] %s\n", time_buf, level_name(level), message);
    }
}
