/**
 * @file tray_internal.h
 * @brief Internal structures for system tray
 */

#ifndef TYPIO_TRAY_INTERNAL_H
#define TYPIO_TRAY_INTERNAL_H

#include "tray.h"
#include <dbus/dbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* D-Bus service names and paths */
#define SNI_WATCHER_SERVICE "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_PATH "/StatusNotifierWatcher"
#define SNI_WATCHER_INTERFACE "org.kde.StatusNotifierWatcher"

#define SNI_ITEM_INTERFACE "org.kde.StatusNotifierItem"
#define SNI_ITEM_PATH "/StatusNotifierItem"

#define DBUS_PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"

/* Menu interface */
#define DBUSMENU_INTERFACE "com.canonical.dbusmenu"
#define DBUSMENU_PATH "/MenuBar"

/**
 * @brief Main tray structure
 */
struct TypioTray {
    /* Typio instance */
    TypioInstance *instance;

    /* D-Bus connection */
    DBusConnection *conn;

    /* Service name */
    char *service_name;             /* e.g., org.kde.StatusNotifierItem-PID-N */

    /* State */
    bool registered;
    TypioTrayStatus status;

    /* Properties */
    char *icon_name;
    char *icon_theme_path;
    char *attention_icon_name;
    char *tooltip_title;
    char *tooltip_description;
    char *title;

    /* Current engine info */
    char *engine_name;
    bool engine_active;

    /* Menu revision */
    uint32_t menu_revision;

    /* Callbacks */
    TypioTrayMenuCallback menu_callback;
    void *user_data;
};

/* SNI implementation functions */
int typio_tray_sni_register(TypioTray *tray);
void typio_tray_sni_emit_signal(TypioTray *tray, const char *signal_name);

/* D-Bus message handlers */
DBusHandlerResult typio_tray_handle_message(DBusConnection *conn,
                                            DBusMessage *msg,
                                            void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_TRAY_INTERNAL_H */
