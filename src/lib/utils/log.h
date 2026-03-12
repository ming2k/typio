/**
 * @file log.h
 * @brief Logging utilities
 */

#ifndef TYPIO_UTILS_LOG_H
#define TYPIO_UTILS_LOG_H

#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Set global log level */
void typio_log_set_level(TypioLogLevel level);
TypioLogLevel typio_log_get_level(void);

/* Set log callback */
void typio_log_set_callback(TypioLogCallback callback, void *user_data);

/* Logging functions */
void typio_log(TypioLogLevel level, const char *format, ...);

#define typio_log_debug(...) typio_log(TYPIO_LOG_DEBUG, __VA_ARGS__)
#define typio_log_info(...) typio_log(TYPIO_LOG_INFO, __VA_ARGS__)
#define typio_log_warning(...) typio_log(TYPIO_LOG_WARNING, __VA_ARGS__)
#define typio_log_error(...) typio_log(TYPIO_LOG_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_UTILS_LOG_H */
