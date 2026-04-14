/**
 * @file notifications.c
 * @brief Desktop notification transport via org.freedesktop.Notifications
 */

#include "notifications.h"

#include "utils/log.h"

#include <dbus/dbus.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#define TYPIO_NOTIFY_SERVICE "org.freedesktop.Notifications"
#define TYPIO_NOTIFY_PATH "/org/freedesktop/Notifications"
#define TYPIO_NOTIFY_INTERFACE "org.freedesktop.Notifications"
#define TYPIO_NOTIFY_RECENT_CAP 16

typedef struct TypioRecentNotification {
    char key[96];
    uint64_t last_sent_ms;
} TypioRecentNotification;

struct TypioNotifier {
    DBusConnection *conn;
    TypioRecentNotification recent[TYPIO_NOTIFY_RECENT_CAP];
    size_t next_recent_slot;
};

static uint64_t typio_notify_monotonic_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

static bool notifier_is_rate_limited(TypioNotifier *notifier,
                                     const char *key,
                                     uint64_t cooldown_ms) {
    uint64_t now_ms;

    if (!notifier || !key || !*key || cooldown_ms == 0) {
        return false;
    }

    now_ms = typio_notify_monotonic_ms();
    for (size_t i = 0; i < TYPIO_NOTIFY_RECENT_CAP; ++i) {
        TypioRecentNotification *entry = &notifier->recent[i];
        if (entry->key[0] == '\0' || strcmp(entry->key, key) != 0) {
            continue;
        }
        if (now_ms >= entry->last_sent_ms &&
            now_ms - entry->last_sent_ms < cooldown_ms) {
            return true;
        }
        entry->last_sent_ms = now_ms;
        return false;
    }

    {
        TypioRecentNotification *entry =
            &notifier->recent[notifier->next_recent_slot % TYPIO_NOTIFY_RECENT_CAP];
        snprintf(entry->key, sizeof(entry->key), "%s", key);
        entry->last_sent_ms = now_ms;
        notifier->next_recent_slot =
            (notifier->next_recent_slot + 1U) % TYPIO_NOTIFY_RECENT_CAP;
    }
    return false;
}

static dbus_bool_t append_hints(DBusMessageIter *iter,
                                TypioNotificationUrgency urgency) {
    DBusMessageIter dict;
    DBusMessageIter entry;
    DBusMessageIter variant;
    const char *urgency_key = "urgency";
    unsigned char urgency_value = (unsigned char)urgency;

    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict)) {
        return FALSE;
    }
    if (!dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &urgency_key)) {
        return FALSE;
    }
    if (!dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "y", &variant)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_BYTE, &urgency_value)) {
        return FALSE;
    }
    if (!dbus_message_iter_close_container(&entry, &variant)) {
        return FALSE;
    }
    if (!dbus_message_iter_close_container(&dict, &entry)) {
        return FALSE;
    }
    return dbus_message_iter_close_container(iter, &dict);
}

TypioNotifier *typio_notifier_new(void) {
    DBusError err;
    TypioNotifier *notifier;

    notifier = calloc(1, sizeof(*notifier));
    if (!notifier) {
        return nullptr;
    }

    dbus_error_init(&err);
    notifier->conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        typio_log_warning("Desktop notifications unavailable: %s", err.message);
        dbus_error_free(&err);
        free(notifier);
        return nullptr;
    }

    if (!notifier->conn) {
        free(notifier);
        return nullptr;
    }

    return notifier;
}

void typio_notifier_free(TypioNotifier *notifier) {
    if (!notifier) {
        return;
    }

    if (notifier->conn) {
        dbus_connection_unref(notifier->conn);
    }
    free(notifier);
}

bool typio_notifier_send(TypioNotifier *notifier,
                         TypioNotificationUrgency urgency,
                         const char *summary,
                         const char *body) {
    DBusMessage *msg;
    DBusMessage *reply;
    DBusMessageIter iter;
    DBusMessageIter actions;
    DBusError err;
    const char *app_name = "Typio";
    const char *app_icon = "typio-keyboard";
    const char *notification_summary = summary ? summary : "Typio";
    const char *notification_body = body ? body : "";
    dbus_uint32_t replaces_id = 0;
    int expire_timeout = urgency == TYPIO_NOTIFICATION_CRITICAL ? 0 : 12000;

    if (!notifier || !notifier->conn || !summary || !*summary) {
        return false;
    }

    msg = dbus_message_new_method_call(TYPIO_NOTIFY_SERVICE,
                                       TYPIO_NOTIFY_PATH,
                                       TYPIO_NOTIFY_INTERFACE,
                                       "Notify");
    if (!msg) {
        return false;
    }

    dbus_message_iter_init_append(msg, &iter);
    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &app_name) ||
        !dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &replaces_id) ||
        !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &app_icon) ||
        !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &notification_summary) ||
        !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &notification_body)) {
        dbus_message_unref(msg);
        return false;
    }

    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &actions) ||
        !dbus_message_iter_close_container(&iter, &actions) ||
        !append_hints(&iter, urgency) ||
        !dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &expire_timeout)) {
        dbus_message_unref(msg);
        return false;
    }

    dbus_error_init(&err);
    reply = dbus_connection_send_with_reply_and_block(notifier->conn, msg, 1500, &err);
    dbus_message_unref(msg);

    if (dbus_error_is_set(&err)) {
        typio_log_debug("Notification send failed: %s", err.message);
        dbus_error_free(&err);
        return false;
    }

    if (reply) {
        dbus_message_unref(reply);
    }
    return true;
}

bool typio_notifier_send_coalesced(TypioNotifier *notifier,
                                   const char *key,
                                   uint64_t cooldown_ms,
                                   TypioNotificationUrgency urgency,
                                   const char *summary,
                                   const char *body) {
    if (notifier_is_rate_limited(notifier, key, cooldown_ms)) {
        return true;
    }
    return typio_notifier_send(notifier, urgency, summary, body);
}
