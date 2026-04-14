/**
 * @file health.h
 * @brief Startup health checks for user-facing notifications
 */

#ifndef TYPIO_NOTIFY_HEALTH_H
#define TYPIO_NOTIFY_HEALTH_H

#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TYPIO_STARTUP_ISSUE_WARNING = 0,
    TYPIO_STARTUP_ISSUE_ERROR = 1,
} TypioStartupIssueSeverity;

typedef struct TypioStartupIssue {
    TypioStartupIssueSeverity severity;
    char code[64];
    char title[128];
    char body[384];
} TypioStartupIssue;

bool typio_notifications_enabled(TypioInstance *instance);
bool typio_startup_notifications_enabled(TypioInstance *instance);
bool typio_startup_checks_enabled(TypioInstance *instance);
bool typio_runtime_notifications_enabled(TypioInstance *instance);
bool typio_voice_notifications_enabled(TypioInstance *instance);
uint64_t typio_notification_cooldown_ms(TypioInstance *instance,
                                        uint64_t default_value);

size_t typio_startup_health_collect(TypioInstance *instance,
                                    TypioStartupIssue *issues,
                                    size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_NOTIFY_HEALTH_H */
