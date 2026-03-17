/**
 * @file status.c
 * @brief D-Bus status interface for structured Typio state
 */

#include "status.h"

#include "../dbus_helpers.h"
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
            ok = typio_dbus_append_dict_entry_string(dict, prefixed, value->data.string_val);
            break;
        case TYPIO_CONFIG_INT:
            ok = typio_dbus_append_dict_entry_int32(dict, prefixed, value->data.int_val);
            break;
        case TYPIO_CONFIG_BOOL:
            ok = typio_dbus_append_dict_entry_bool(dict, prefixed, value->data.bool_val ? TRUE : FALSE);
            break;
        case TYPIO_CONFIG_FLOAT:
            ok = typio_dbus_append_dict_entry_double(dict, prefixed, value->data.float_val);
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
    const char *engine_name;
    dbus_bool_t ok = TRUE;

    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict)) {
        return FALSE;
    }

    engine = status_active_engine(bus);
    info = engine ? engine->info : nullptr;
    config_path = engine ? typio_engine_get_config_path(engine) : nullptr;
    engine_name = engine ? typio_engine_get_name(engine) : nullptr;

    if (info) {
        ok = typio_dbus_append_dict_entry_string(&dict, "name", info->name) &&
             typio_dbus_append_dict_entry_string(&dict, "display_name", info->display_name) &&
             typio_dbus_append_dict_entry_string(&dict, "icon", info->icon) &&
             typio_dbus_append_dict_entry_string(&dict, "language", info->language) &&
             typio_dbus_append_dict_entry_string(&dict, "engine_type", engine_type_name(info->type)) &&
             typio_dbus_append_dict_entry_uint32(&dict, "capabilities", info->capabilities);
    }

    if (ok && engine) {
        ok = typio_dbus_append_dict_entry_bool(&dict, "active",
                                               typio_engine_is_active(engine) ? TRUE : FALSE);
    }

    if (ok && config_path && *config_path) {
        ok = typio_dbus_append_dict_entry_string(&dict, "config_path", config_path);
    }

    if (ok && bus && bus->instance && engine_name && *engine_name) {
        config = typio_instance_get_engine_config(bus->instance, engine_name);
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

static dbus_bool_t append_ordered_engines_array(DBusMessageIter *iter,
                                                TypioStatusBus *bus) {
    DBusMessageIter array;
    TypioEngineManager *manager;
    const char **engines;
    size_t count = 0;

    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "s", &array)) {
        return FALSE;
    }

    manager = bus ? typio_instance_get_engine_manager(bus->instance) : nullptr;
    engines = manager ? typio_engine_manager_list_ordered_keyboards(manager, &count) : nullptr;
    for (size_t i = 0; i < count; ++i) {
        const char *name = engines[i];
        if (!dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &name)) {
            return FALSE;
        }
    }

    return dbus_message_iter_close_container(iter, &array);
}

static dbus_bool_t append_engine_order_array(DBusMessageIter *iter,
                                             TypioStatusBus *bus) {
    DBusMessageIter array;
    TypioConfig *config;
    size_t count = 0;

    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "s", &array)) {
        return FALSE;
    }

    config = bus && bus->instance ? typio_instance_get_config(bus->instance) : NULL;
    count = config ? typio_config_get_array_size(config, "engine_order") : 0;
    for (size_t i = 0; i < count; ++i) {
        const char *name = typio_config_get_array_string(config, "engine_order", i);
        if (!name) {
            continue;
        }
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
    TypioEngineManager *manager = bus ? typio_instance_get_engine_manager(bus->instance) : nullptr;
    TypioEngine *voice_engine = manager ? typio_engine_manager_get_active_voice(manager) : nullptr;
    const char *active_name = engine ? typio_engine_get_name(engine) : "";
    const char *active_voice_name = voice_engine ? typio_engine_get_name(voice_engine) : "";
    char *config_text;

    if (strcmp(property, TYPIO_STATUS_PROP_VERSION) == 0) {
        const char *version = TYPIO_VERSION;
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &variant)) {
            return FALSE;
        }
        if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &version)) {
            return FALSE;
        }
        return dbus_message_iter_close_container(iter, &variant);
    }

    if (strcmp(property, TYPIO_STATUS_PROP_ACTIVE_ENGINE) == 0) {
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &variant)) {
            return FALSE;
        }
        if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &active_name)) {
            return FALSE;
        }
        return dbus_message_iter_close_container(iter, &variant);
    }

    if (strcmp(property, TYPIO_STATUS_PROP_AVAILABLE_ENGINES) == 0) {
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "as", &variant)) {
            return FALSE;
        }
        if (!append_available_engines_array(&variant, bus)) {
            return FALSE;
        }
        return dbus_message_iter_close_container(iter, &variant);
    }

    if (strcmp(property, TYPIO_STATUS_PROP_ORDERED_ENGINES) == 0) {
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "as", &variant)) {
            return FALSE;
        }
        if (!append_ordered_engines_array(&variant, bus)) {
            return FALSE;
        }
        return dbus_message_iter_close_container(iter, &variant);
    }

    if (strcmp(property, TYPIO_STATUS_PROP_ENGINE_ORDER) == 0) {
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "as", &variant)) {
            return FALSE;
        }
        if (!append_engine_order_array(&variant, bus)) {
            return FALSE;
        }
        return dbus_message_iter_close_container(iter, &variant);
    }

    if (strcmp(property, TYPIO_STATUS_PROP_ACTIVE_VOICE_ENGINE) == 0) {
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &variant)) {
            return FALSE;
        }
        if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &active_voice_name)) {
            return FALSE;
        }
        return dbus_message_iter_close_container(iter, &variant);
    }

    if (strcmp(property, TYPIO_STATUS_PROP_ACTIVE_ENGINE_STATE) == 0) {
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "a{sv}", &variant)) {
            return FALSE;
        }
        if (!append_active_engine_state_dict(&variant, bus)) {
            return FALSE;
        }
        return dbus_message_iter_close_container(iter, &variant);
    }

    if (strcmp(property, TYPIO_STATUS_PROP_CONFIG_TEXT) == 0) {
        const char *text = "";
        config_text = (bus && bus->instance) ? typio_instance_get_config_text(bus->instance) : nullptr;
        if (config_text) {
            text = config_text;
        }
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &variant)) {
            free(config_text);
            return FALSE;
        }
        if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &text)) {
            free(config_text);
            return FALSE;
        }
        free(config_text);
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
        TYPIO_STATUS_PROP_VERSION,
        TYPIO_STATUS_PROP_ACTIVE_ENGINE,
        TYPIO_STATUS_PROP_AVAILABLE_ENGINES,
        TYPIO_STATUS_PROP_ORDERED_ENGINES,
        TYPIO_STATUS_PROP_ENGINE_ORDER,
        TYPIO_STATUS_PROP_ACTIVE_VOICE_ENGINE,
        TYPIO_STATUS_PROP_ACTIVE_ENGINE_STATE,
        TYPIO_STATUS_PROP_CONFIG_TEXT,
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

    if (bus && bus->instance) {
        TypioConfig *config = typio_instance_get_config(bus->instance);
        if (config) {
            typio_config_set_string(config, "default_engine", engine_name);
            typio_instance_save_config(bus->instance);
        }
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

static DBusMessage *status_handle_set_config_text(TypioStatusBus *bus,
                                                  DBusMessage *msg) {
    const char *config_text = nullptr;
    TypioResult result;

    if (!dbus_message_get_args(msg, nullptr,
                               DBUS_TYPE_STRING, &config_text,
                               DBUS_TYPE_INVALID) ||
        !config_text) {
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                      "SetConfigText expects a configuration string");
    }

    result = bus ? typio_instance_set_config_text(bus->instance, config_text) : TYPIO_ERROR;
    if (result != TYPIO_OK) {
        return dbus_message_new_error(msg, DBUS_ERROR_FAILED,
                                      "Failed to save Typio configuration text");
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
            "    <property name=\"" TYPIO_STATUS_PROP_VERSION "\" type=\"s\" access=\"read\"/>\n"
            "    <property name=\"" TYPIO_STATUS_PROP_ACTIVE_ENGINE "\" type=\"s\" access=\"read\"/>\n"
            "    <property name=\"" TYPIO_STATUS_PROP_AVAILABLE_ENGINES "\" type=\"as\" access=\"read\"/>\n"
            "    <property name=\"" TYPIO_STATUS_PROP_ORDERED_ENGINES "\" type=\"as\" access=\"read\"/>\n"
            "    <property name=\"" TYPIO_STATUS_PROP_ENGINE_ORDER "\" type=\"as\" access=\"read\"/>\n"
            "    <property name=\"" TYPIO_STATUS_PROP_ACTIVE_VOICE_ENGINE "\" type=\"s\" access=\"read\"/>\n"
            "    <property name=\"" TYPIO_STATUS_PROP_ACTIVE_ENGINE_STATE "\" type=\"a{sv}\" access=\"read\"/>\n"
            "    <property name=\"" TYPIO_STATUS_PROP_CONFIG_TEXT "\" type=\"s\" access=\"read\"/>\n"
            "    <method name=\"" TYPIO_STATUS_METHOD_ACTIVATE_ENGINE "\"><arg name=\"engine\" type=\"s\" direction=\"in\"/></method>\n"
            "    <method name=\"" TYPIO_STATUS_METHOD_SET_CONFIG_TEXT "\"><arg name=\"content\" type=\"s\" direction=\"in\"/></method>\n"
            "    <method name=\"" TYPIO_STATUS_METHOD_RELOAD_CONFIG "\"/>\n"
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
        if (strcmp(member, TYPIO_STATUS_METHOD_ACTIVATE_ENGINE) == 0) {
            reply = status_handle_activate_engine(bus, msg);
        } else if (strcmp(member, TYPIO_STATUS_METHOD_SET_CONFIG_TEXT) == 0) {
            reply = status_handle_set_config_text(bus, msg);
        } else if (strcmp(member, TYPIO_STATUS_METHOD_RELOAD_CONFIG) == 0) {
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
        TYPIO_STATUS_PROP_ACTIVE_ENGINE,
        TYPIO_STATUS_PROP_AVAILABLE_ENGINES,
        TYPIO_STATUS_PROP_ORDERED_ENGINES,
        TYPIO_STATUS_PROP_ENGINE_ORDER,
        TYPIO_STATUS_PROP_ACTIVE_VOICE_ENGINE,
        TYPIO_STATUS_PROP_ACTIVE_ENGINE_STATE,
        TYPIO_STATUS_PROP_CONFIG_TEXT,
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
