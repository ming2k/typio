/**
 * @file status.c
 * @brief D-Bus status interface for structured Typio state
 */

#include "status.h"

#include "typio/config.h"
#include "typio/engine_manager.h"
#include "typio/instance.h"
#include "typio/typio.h"
#include "typio_build_config.h"
#include "utils/log.h"
#include "utils/string.h"

#include <dbus/dbus.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DBUS_PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"

struct TypioStatusBus {
    TypioInstance *instance;
    DBusConnection *conn;
};

static dbus_bool_t append_dict_entry_string(DBusMessageIter *dict,
                                            const char *key,
                                            const char *value) {
    DBusMessageIter entry;
    DBusMessageIter variant;
    const char *text = value ? value : "";

    if (!dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key)) {
        return FALSE;
    }
    if (!dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &text)) {
        return FALSE;
    }
    if (!dbus_message_iter_close_container(&entry, &variant)) {
        return FALSE;
    }
    return dbus_message_iter_close_container(dict, &entry);
}

static dbus_bool_t append_dict_entry_bool(DBusMessageIter *dict,
                                          const char *key,
                                          dbus_bool_t value) {
    DBusMessageIter entry;
    DBusMessageIter variant;

    if (!dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key)) {
        return FALSE;
    }
    if (!dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value)) {
        return FALSE;
    }
    if (!dbus_message_iter_close_container(&entry, &variant)) {
        return FALSE;
    }
    return dbus_message_iter_close_container(dict, &entry);
}

static dbus_bool_t append_dict_entry_int32(DBusMessageIter *dict,
                                           const char *key,
                                           dbus_int32_t value) {
    DBusMessageIter entry;
    DBusMessageIter variant;

    if (!dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key)) {
        return FALSE;
    }
    if (!dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &variant)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &value)) {
        return FALSE;
    }
    if (!dbus_message_iter_close_container(&entry, &variant)) {
        return FALSE;
    }
    return dbus_message_iter_close_container(dict, &entry);
}

static dbus_bool_t append_dict_entry_uint32(DBusMessageIter *dict,
                                            const char *key,
                                            dbus_uint32_t value) {
    DBusMessageIter entry;
    DBusMessageIter variant;

    if (!dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key)) {
        return FALSE;
    }
    if (!dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &variant)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &value)) {
        return FALSE;
    }
    if (!dbus_message_iter_close_container(&entry, &variant)) {
        return FALSE;
    }
    return dbus_message_iter_close_container(dict, &entry);
}

static dbus_bool_t append_dict_entry_double(DBusMessageIter *dict,
                                            const char *key,
                                            double value) {
    DBusMessageIter entry;
    DBusMessageIter variant;

    if (!dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key)) {
        return FALSE;
    }
    if (!dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "d", &variant)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_DOUBLE, &value)) {
        return FALSE;
    }
    if (!dbus_message_iter_close_container(&entry, &variant)) {
        return FALSE;
    }
    return dbus_message_iter_close_container(dict, &entry);
}

static const char *engine_type_name(TypioEngineType type) {
    return type == TYPIO_ENGINE_TYPE_VOICE ? "voice" : "keyboard";
}

static TypioEngine *status_active_engine(TypioStatusBus *bus) {
    TypioEngineManager *manager;

    if (!bus || !bus->instance) {
        return nullptr;
    }

    manager = typio_instance_get_engine_manager(bus->instance);
    return manager ? typio_engine_manager_get_active(manager) : nullptr;
}

static dbus_bool_t append_config_entries(DBusMessageIter *dict,
                                         const TypioConfig *config) {
    size_t count;

    if (!dict || !config) {
        return TRUE;
    }

    count = typio_config_key_count(config);
    for (size_t i = 0; i < count; ++i) {
        const char *key = typio_config_key_at(config, i);
        const TypioConfigValue *value;
        char *prefixed;
        bool ok = true;

        if (!key || !*key) {
            continue;
        }

        value = typio_config_get(config, key);
        if (!value) {
            continue;
        }

        prefixed = calloc(strlen("config.") + strlen(key) + 1U, sizeof(char));
        if (!prefixed) {
            return FALSE;
        }
        snprintf(prefixed, strlen("config.") + strlen(key) + 1U, "config.%s", key);

        switch (value->type) {
        case TYPIO_CONFIG_STRING:
            ok = append_dict_entry_string(dict, prefixed, value->data.string_val);
            break;
        case TYPIO_CONFIG_INT:
            ok = append_dict_entry_int32(dict, prefixed, value->data.int_val);
            break;
        case TYPIO_CONFIG_BOOL:
            ok = append_dict_entry_bool(dict, prefixed, value->data.bool_val ? TRUE : FALSE);
            break;
        case TYPIO_CONFIG_FLOAT:
            ok = append_dict_entry_double(dict, prefixed, value->data.float_val);
            break;
        default:
            ok = true;
            break;
        }

        free(prefixed);
        if (!ok) {
            return FALSE;
        }
    }

    return TRUE;
}

static dbus_bool_t append_active_engine_state_dict(DBusMessageIter *iter,
                                                   TypioStatusBus *bus) {
    DBusMessageIter dict;
    TypioEngine *engine;
    TypioConfig *config = nullptr;
    const TypioEngineInfo *info;
    const char *config_path;
    dbus_bool_t ok = TRUE;

    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict)) {
        return FALSE;
    }

    engine = status_active_engine(bus);
    info = engine ? engine->info : nullptr;
    config_path = engine ? typio_engine_get_config_path(engine) : nullptr;

    if (info) {
        ok = append_dict_entry_string(&dict, "name", info->name) &&
             append_dict_entry_string(&dict, "display_name", info->display_name) &&
             append_dict_entry_string(&dict, "icon", info->icon) &&
             append_dict_entry_string(&dict, "language", info->language) &&
             append_dict_entry_string(&dict, "engine_type", engine_type_name(info->type)) &&
             append_dict_entry_uint32(&dict, "capabilities", info->capabilities);
    }

    if (ok && engine) {
        ok = append_dict_entry_bool(&dict, "active",
                                    typio_engine_is_active(engine) ? TRUE : FALSE);
    }

    if (ok && config_path && *config_path) {
        ok = append_dict_entry_string(&dict, "config_path", config_path);
    }

    if (ok && config_path && *config_path) {
        config = typio_config_load_file(config_path);
        if (config) {
            ok = append_config_entries(&dict, config);
            typio_config_free(config);
        }
    }

    if (!dbus_message_iter_close_container(iter, &dict)) {
        return FALSE;
    }

    return ok;
}

static dbus_bool_t append_available_engines_array(DBusMessageIter *iter,
                                                  TypioStatusBus *bus) {
    DBusMessageIter array;
    TypioEngineManager *manager;
    const char **engines;
    size_t count = 0;

    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "s", &array)) {
        return FALSE;
    }

    manager = bus ? typio_instance_get_engine_manager(bus->instance) : nullptr;
    engines = manager ? typio_engine_manager_list(manager, &count) : nullptr;
    for (size_t i = 0; i < count; ++i) {
        const char *name = engines[i];
        if (!dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &name)) {
            return FALSE;
        }
    }

    return dbus_message_iter_close_container(iter, &array);
}

static dbus_bool_t append_property_variant(DBusMessageIter *iter,
                                           TypioStatusBus *bus,
                                           const char *property) {
    DBusMessageIter variant;
    TypioEngine *engine = status_active_engine(bus);
    const char *active_name = engine ? typio_engine_get_name(engine) : "";

    if (strcmp(property, "Version") == 0) {
        const char *version = TYPIO_VERSION;
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &variant)) {
            return FALSE;
        }
        if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &version)) {
            return FALSE;
        }
        return dbus_message_iter_close_container(iter, &variant);
    }

    if (strcmp(property, "ActiveEngine") == 0) {
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &variant)) {
            return FALSE;
        }
        if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &active_name)) {
            return FALSE;
        }
        return dbus_message_iter_close_container(iter, &variant);
    }

    if (strcmp(property, "AvailableEngines") == 0) {
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "as", &variant)) {
            return FALSE;
        }
        if (!append_available_engines_array(&variant, bus)) {
            return FALSE;
        }
        return dbus_message_iter_close_container(iter, &variant);
    }

    if (strcmp(property, "ActiveEngineState") == 0) {
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "a{sv}", &variant)) {
            return FALSE;
        }
        if (!append_active_engine_state_dict(&variant, bus)) {
            return FALSE;
        }
        return dbus_message_iter_close_container(iter, &variant);
    }

    return FALSE;
}

static DBusMessage *status_handle_properties_get(TypioStatusBus *bus,
                                                 DBusMessage *msg) {
    const char *interface;
    const char *property;
    DBusMessage *reply;
    DBusMessageIter iter;

    if (!dbus_message_get_args(msg, nullptr,
                               DBUS_TYPE_STRING, &interface,
                               DBUS_TYPE_STRING, &property,
                               DBUS_TYPE_INVALID)) {
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "Invalid arguments");
    }

    if (strcmp(interface, TYPIO_STATUS_DBUS_INTERFACE) != 0) {
        return dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_INTERFACE, "Unknown interface");
    }

    reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return nullptr;
    }

    dbus_message_iter_init_append(reply, &iter);
    if (!append_property_variant(&iter, bus, property)) {
        dbus_message_unref(reply);
        return dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property");
    }

    return reply;
}

static DBusMessage *status_handle_properties_getall(TypioStatusBus *bus,
                                                    DBusMessage *msg) {
    const char *interface;
    DBusMessage *reply;
    DBusMessageIter iter;
    DBusMessageIter dict;
    const char *properties[] = {
        "Version",
        "ActiveEngine",
        "AvailableEngines",
        "ActiveEngineState",
    };

    if (!dbus_message_get_args(msg, nullptr,
                               DBUS_TYPE_STRING, &interface,
                               DBUS_TYPE_INVALID)) {
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "Invalid arguments");
    }

    if (strcmp(interface, TYPIO_STATUS_DBUS_INTERFACE) != 0) {
        return dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_INTERFACE, "Unknown interface");
    }

    reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return nullptr;
    }

    dbus_message_iter_init_append(reply, &iter);
    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict)) {
        dbus_message_unref(reply);
        return nullptr;
    }

    for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]); ++i) {
        DBusMessageIter entry;
        if (!dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry)) {
            dbus_message_unref(reply);
            return nullptr;
        }
        if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &properties[i]) ||
            !append_property_variant(&entry, bus, properties[i]) ||
            !dbus_message_iter_close_container(&dict, &entry)) {
            dbus_message_unref(reply);
            return nullptr;
        }
    }

    if (!dbus_message_iter_close_container(&iter, &dict)) {
        dbus_message_unref(reply);
        return nullptr;
    }

    return reply;
}

static DBusMessage *status_handle_activate_engine(TypioStatusBus *bus,
                                                  DBusMessage *msg) {
    const char *engine_name = nullptr;
    TypioEngineManager *manager;
    TypioResult result;

    if (!dbus_message_get_args(msg, nullptr,
                               DBUS_TYPE_STRING, &engine_name,
                               DBUS_TYPE_INVALID) ||
        !engine_name || !*engine_name) {
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                      "ActivateEngine expects a non-empty engine name");
    }

    manager = bus ? typio_instance_get_engine_manager(bus->instance) : nullptr;
    if (!manager) {
        return dbus_message_new_error(msg, DBUS_ERROR_FAILED,
                                      "Engine manager not available");
    }

    result = typio_engine_manager_set_active(manager, engine_name);
    if (result != TYPIO_OK) {
        return dbus_message_new_error(msg, DBUS_ERROR_FAILED,
                                      "Failed to activate requested engine");
    }

    typio_status_bus_emit_properties_changed(bus);
    return dbus_message_new_method_return(msg);
}

static DBusMessage *status_handle_reload_config(TypioStatusBus *bus,
                                                DBusMessage *msg) {
    TypioResult result;

    result = bus ? typio_instance_reload_config(bus->instance) : TYPIO_ERROR;
    if (result != TYPIO_OK) {
        return dbus_message_new_error(msg, DBUS_ERROR_FAILED,
                                      "Failed to reload Typio configuration");
    }

    typio_status_bus_emit_properties_changed(bus);
    return dbus_message_new_method_return(msg);
}

static DBusHandlerResult status_message_handler([[maybe_unused]] DBusConnection *conn,
                                                DBusMessage *msg,
                                                void *user_data) {
    TypioStatusBus *bus = user_data;
    const char *member = dbus_message_get_member(msg);
    const char *interface = dbus_message_get_interface(msg);
    const char *path = dbus_message_get_path(msg);
    DBusMessage *reply = nullptr;

    if (!bus || !msg || dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (!path || strcmp(path, TYPIO_STATUS_DBUS_PATH) != 0) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (!interface) {
        interface = "";
    }

    if (strcmp(interface, DBUS_PROPERTIES_INTERFACE) == 0 ||
        strcmp(member, "Get") == 0 || strcmp(member, "GetAll") == 0) {
        if (strcmp(member, "Get") == 0) {
            reply = status_handle_properties_get(bus, msg);
        } else if (strcmp(member, "GetAll") == 0) {
            reply = status_handle_properties_getall(bus, msg);
        }
    } else if (strcmp(member, "Introspect") == 0) {
        const char *xml =
            "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
            "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
            "<node>\n"
            "  <interface name=\"org.typio.InputMethod1\">\n"
            "    <property name=\"Version\" type=\"s\" access=\"read\"/>\n"
            "    <property name=\"ActiveEngine\" type=\"s\" access=\"read\"/>\n"
            "    <property name=\"AvailableEngines\" type=\"as\" access=\"read\"/>\n"
            "    <property name=\"ActiveEngineState\" type=\"a{sv}\" access=\"read\"/>\n"
            "    <method name=\"ActivateEngine\"><arg name=\"engine\" type=\"s\" direction=\"in\"/></method>\n"
            "    <method name=\"ReloadConfig\"/>\n"
            "  </interface>\n"
            "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
            "    <method name=\"Get\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"out\"/></method>\n"
            "    <method name=\"GetAll\"><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"out\"/></method>\n"
            "    <signal name=\"PropertiesChanged\"><arg type=\"s\"/><arg type=\"a{sv}\"/><arg type=\"as\"/></signal>\n"
            "  </interface>\n"
            "</node>\n";
        reply = dbus_message_new_method_return(msg);
        if (reply) {
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
        }
    } else if (strcmp(interface, TYPIO_STATUS_DBUS_INTERFACE) == 0 ||
               interface[0] == '\0') {
        if (strcmp(member, "ActivateEngine") == 0) {
            reply = status_handle_activate_engine(bus, msg);
        } else if (strcmp(member, "ReloadConfig") == 0) {
            reply = status_handle_reload_config(bus, msg);
        }
    }

    if (!reply) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_connection_send(bus->conn, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static const DBusObjectPathVTable status_vtable = {
    .message_function = status_message_handler,
};

TypioStatusBus *typio_status_bus_new(TypioInstance *instance) {
    TypioStatusBus *bus;
    DBusError err;
    int ret;

    if (!instance) {
        return nullptr;
    }

    bus = calloc(1, sizeof(TypioStatusBus));
    if (!bus) {
        return nullptr;
    }

    bus->instance = instance;

    dbus_error_init(&err);
    bus->conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        typio_log(TYPIO_LOG_WARNING,
                  "Failed to connect status bus to session D-Bus: %s",
                  err.message);
        dbus_error_free(&err);
        typio_status_bus_destroy(bus);
        return nullptr;
    }

    ret = dbus_bus_request_name(bus->conn, TYPIO_STATUS_DBUS_SERVICE,
                                DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err) || ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        typio_log(TYPIO_LOG_WARNING,
                  "Failed to acquire status D-Bus name %s",
                  TYPIO_STATUS_DBUS_SERVICE);
        if (dbus_error_is_set(&err)) {
            dbus_error_free(&err);
        }
        typio_status_bus_destroy(bus);
        return nullptr;
    }

    if (!dbus_connection_register_object_path(bus->conn, TYPIO_STATUS_DBUS_PATH,
                                              &status_vtable, bus)) {
        typio_log(TYPIO_LOG_WARNING,
                  "Failed to register status D-Bus object path");
        typio_status_bus_destroy(bus);
        return nullptr;
    }

    typio_log(TYPIO_LOG_INFO,
              "D-Bus status interface exported at %s",
              TYPIO_STATUS_DBUS_SERVICE);
    return bus;
}

void typio_status_bus_destroy(TypioStatusBus *bus) {
    if (!bus) {
        return;
    }

    if (bus->conn) {
        dbus_connection_unregister_object_path(bus->conn, TYPIO_STATUS_DBUS_PATH);
        dbus_connection_unref(bus->conn);
    }

    free(bus);
}

int typio_status_bus_get_fd(TypioStatusBus *bus) {
    int fd = -1;

    if (!bus || !bus->conn) {
        return -1;
    }

    if (!dbus_connection_get_unix_fd(bus->conn, &fd)) {
        return -1;
    }

    return fd;
}

int typio_status_bus_dispatch(TypioStatusBus *bus) {
    if (!bus || !bus->conn) {
        return -1;
    }

    dbus_connection_read_write(bus->conn, 0);
    while (dbus_connection_dispatch(bus->conn) == DBUS_DISPATCH_DATA_REMAINS) {
    }

    return 0;
}

void typio_status_bus_emit_properties_changed(TypioStatusBus *bus) {
    DBusMessage *sig;
    DBusMessageIter iter;
    DBusMessageIter changed;
    DBusMessageIter invalidated;
    const char *interface = TYPIO_STATUS_DBUS_INTERFACE;
    const char *properties[] = {
        "ActiveEngine",
        "AvailableEngines",
        "ActiveEngineState",
    };

    if (!bus || !bus->conn) {
        return;
    }

    sig = dbus_message_new_signal(TYPIO_STATUS_DBUS_PATH,
                                  DBUS_PROPERTIES_INTERFACE,
                                  "PropertiesChanged");
    if (!sig) {
        return;
    }

    dbus_message_iter_init_append(sig, &iter);
    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &interface)) {
        dbus_message_unref(sig);
        return;
    }

    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &changed)) {
        dbus_message_unref(sig);
        return;
    }

    for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]); ++i) {
        DBusMessageIter entry;
        if (!dbus_message_iter_open_container(&changed, DBUS_TYPE_DICT_ENTRY, nullptr, &entry) ||
            !dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &properties[i]) ||
            !append_property_variant(&entry, bus, properties[i]) ||
            !dbus_message_iter_close_container(&changed, &entry)) {
            dbus_message_unref(sig);
            return;
        }
    }

    if (!dbus_message_iter_close_container(&iter, &changed) ||
        !dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &invalidated) ||
        !dbus_message_iter_close_container(&iter, &invalidated)) {
        dbus_message_unref(sig);
        return;
    }

    dbus_connection_send(bus->conn, sig, nullptr);
    dbus_message_unref(sig);
}
