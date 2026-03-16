/**
 * @file notifications.h
 * @brief Desktop notification transport via org.freedesktop.Notifications
 */

#ifndef TYPIO_NOTIFY_NOTIFICATIONS_H
#define TYPIO_NOTIFY_NOTIFICATIONS_H

#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioNotifier TypioNotifier;

typedef enum {
    TYPIO_NOTIFICATION_LOW = 0,
    TYPIO_NOTIFICATION_NORMAL = 1,
    TYPIO_NOTIFICATION_CRITICAL = 2,
} TypioNotificationUrgency;

TypioNotifier *typio_notifier_new(void);
void typio_notifier_free(TypioNotifier *notifier);

bool typio_notifier_send(TypioNotifier *notifier,
                         TypioNotificationUrgency urgency,
                         const char *summary,
                         const char *body);

bool typio_notifier_send_coalesced(TypioNotifier *notifier,
                                   const char *key,
                                   uint64_t cooldown_ms,
                                   TypioNotificationUrgency urgency,
                                   const char *summary,
                                   const char *body);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_NOTIFY_NOTIFICATIONS_H */
