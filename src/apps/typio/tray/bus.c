/**
 * @file bus.c
 * @brief D-Bus connection and watcher lifecycle for the system tray
 */

#include "tray_internal.h"
#include "typio_build_config.h"
#include "utils/log.h"
#include "utils/string.h"

#define TYPIO_TRAY_BUS_MAX_DISPATCH_PER_TICK 16

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static const char *typio_tray_default_icon_theme_path(void) {
    static char install_theme_path[512];
    static char source_theme_path[512];

    if (snprintf(install_theme_path, sizeof(install_theme_path),
                 "%s/hicolor", TYPIO_INSTALL_ICON_DIR) > 0 &&
        access(install_theme_path, R_OK) == 0) {
        return install_theme_path;
    }

    if (access(TYPIO_INSTALL_ICON_DIR, R_OK) == 0) {
        return TYPIO_INSTALL_ICON_DIR;
    }

    if (snprintf(source_theme_path, sizeof(source_theme_path),
                 "%s/hicolor", TYPIO_SOURCE_ICON_DIR) > 0 &&
        access(source_theme_path, R_OK) == 0) {
        return source_theme_path;
    }

    if (access(TYPIO_SOURCE_ICON_DIR, R_OK) == 0) {
        return TYPIO_SOURCE_ICON_DIR;
    }

    return "";
}

static DBusHandlerResult tray_bus_filter([[maybe_unused]] DBusConnection *conn,
                                         DBusMessage *msg,
                                         void *user_data) {
    TypioTray *tray = user_data;
    const char *name = nullptr;
    const char *old_owner = nullptr;
    const char *new_owner = nullptr;

    if (!tray || !msg ||
        dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL ||
        !dbus_message_is_signal(msg, DBUS_INTERFACE, "NameOwnerChanged")) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (!dbus_message_get_args(msg, nullptr,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_STRING, &old_owner,
                               DBUS_TYPE_STRING, &new_owner,
                               DBUS_TYPE_INVALID) ||
        !name || strcmp(name, SNI_WATCHER_SERVICE) != 0) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (new_owner && new_owner[0] != '\0') {
        typio_log(TYPIO_LOG_INFO,
                  "StatusNotifierWatcher appeared as %s",
                  new_owner);
        if (!tray->registered) {
            typio_tray_sni_register(tray);
        }
    } else {
        typio_log(TYPIO_LOG_INFO, "StatusNotifierWatcher disappeared");
        tray->registered = false;
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static const DBusObjectPathVTable tray_object_vtable = {
    .message_function = typio_tray_handle_message,
};

TypioTray *typio_tray_new(TypioInstance *instance, const TypioTrayConfig *config) {
    DBusError err;
    pid_t pid;
    int ret;
    static int instance_counter = 0;
    TypioTray *tray;
    int service_name_len;

    if (!instance) {
        return nullptr;
    }

    tray = calloc(1, sizeof(TypioTray));
    if (!tray) {
        return nullptr;
    }

    tray->instance = instance;

    if (config) {
        if (config->icon_name) {
            tray->icon_name = typio_strdup(config->icon_name);
        }
        if (config->tooltip) {
            tray->tooltip_title = typio_strdup(config->tooltip);
        }
        tray->menu_callback = config->menu_callback;
        tray->user_data = config->user_data;
    }

    if (!tray->icon_name) {
        tray->icon_name = typio_strdup("typio-keyboard");
    }
    tray->icon_theme_path = typio_strdup(typio_tray_default_icon_theme_path());
    if (!tray->tooltip_title) {
        tray->tooltip_title = typio_strdup("Typio Input Method");
    }
    tray->title = typio_strdup("Typio");

    dbus_error_init(&err);
    tray->conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        typio_log(TYPIO_LOG_ERROR,
                  "Failed to connect to session D-Bus: %s",
                  err.message);
        dbus_error_free(&err);
        typio_tray_destroy(tray);
        return nullptr;
    }

    dbus_bus_add_match(tray->conn, DBUS_NAME_OWNER_CHANGED_WATCHER_MATCH, &err);
    dbus_connection_add_filter(tray->conn, tray_bus_filter, tray, nullptr);
    dbus_connection_flush(tray->conn);
    if (dbus_error_is_set(&err)) {
        typio_log(TYPIO_LOG_WARNING,
                  "Failed to watch StatusNotifierWatcher ownership: %s",
                  err.message);
        dbus_error_free(&err);
    }

    pid = getpid();
    service_name_len = snprintf(nullptr, 0, "org.kde.StatusNotifierItem-%d-%d",
                                (int)pid, instance_counter++);
    if (service_name_len < 0) {
        typio_tray_destroy(tray);
        return nullptr;
    }

    tray->service_name = malloc((size_t)service_name_len + 1);
    if (!tray->service_name) {
        typio_tray_destroy(tray);
        return nullptr;
    }

    if (snprintf(tray->service_name, (size_t)service_name_len + 1,
                 "org.kde.StatusNotifierItem-%d-%d", (int)pid,
                 instance_counter - 1) < 0) {
        typio_tray_destroy(tray);
        return nullptr;
    }

    ret = dbus_bus_request_name(tray->conn, tray->service_name,
                                DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err) || ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        typio_log(TYPIO_LOG_ERROR,
                  "Failed to acquire D-Bus name %s",
                  tray->service_name);
        if (dbus_error_is_set(&err)) {
            dbus_error_free(&err);
        }
        typio_tray_destroy(tray);
        return nullptr;
    }

    if (!dbus_connection_register_object_path(tray->conn, SNI_ITEM_PATH,
                                              &tray_object_vtable, tray)) {
        typio_log(TYPIO_LOG_ERROR, "Failed to register SNI object path");
        typio_tray_destroy(tray);
        return nullptr;
    }

    if (!dbus_connection_register_object_path(tray->conn, DBUSMENU_PATH,
                                              &tray_object_vtable, tray)) {
        typio_log(TYPIO_LOG_ERROR, "Failed to register menu object path");
        typio_tray_destroy(tray);
        return nullptr;
    }

    typio_tray_sni_register(tray);
    return tray;
}

void typio_tray_destroy(TypioTray *tray) {
    if (!tray) {
        return;
    }

    if (tray->conn) {
        DBusError err;

        dbus_error_init(&err);
        dbus_bus_remove_match(tray->conn, DBUS_NAME_OWNER_CHANGED_WATCHER_MATCH, &err);
        if (dbus_error_is_set(&err)) {
            dbus_error_free(&err);
        }
        dbus_connection_remove_filter(tray->conn, tray_bus_filter, tray);
        dbus_connection_unregister_object_path(tray->conn, SNI_ITEM_PATH);
        dbus_connection_unregister_object_path(tray->conn, DBUSMENU_PATH);
        dbus_connection_unref(tray->conn);
    }

    free(tray->service_name);
    free(tray->icon_name);
    free(tray->icon_theme_path);
    free(tray->attention_icon_name);
    free(tray->tooltip_title);
    free(tray->tooltip_description);
    free(tray->title);
    free(tray->engine_name);

    typio_log(TYPIO_LOG_INFO, "System tray destroyed");
    free(tray);
}

int typio_tray_get_fd(TypioTray *tray) {
    int fd = -1;

    if (!tray || !tray->conn) {
        return -1;
    }

    if (!dbus_connection_get_unix_fd(tray->conn, &fd)) {
        return -1;
    }

    return fd;
}

int typio_tray_dispatch(TypioTray *tray) {
    int dispatched = 0;

    if (!tray || !tray->conn) {
        return -1;
    }

    dbus_connection_read_write(tray->conn, 0);
    while (dispatched < TYPIO_TRAY_BUS_MAX_DISPATCH_PER_TICK &&
           dbus_connection_dispatch(tray->conn) == DBUS_DISPATCH_DATA_REMAINS) {
        dispatched++;
    }

    return 0;
}
