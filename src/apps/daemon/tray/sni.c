/**
 * @file sni.c
 * @brief StatusNotifierItem D-Bus implementation using libdbus-1
 */

#include "tray_internal.h"
#include "../dbus_helpers.h"
#include "icon_pixmap.h"
#include "typio/config.h"
#include "typio/rime_schema_list.h"
#include "typio/typio.h"
#include "utils/log.h"
#include "utils/string.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TYPIO_TRAY_RIME_MENU_BASE_ID 200
#define TYPIO_TRAY_RIME_MENU_RELOAD_ID 260
#define TYPIO_TRAY_RIME_MENU_MAX_SCHEMAS 32

typedef struct TypioTrayRimeSchemaInfo {
    char *id;
    char *name;
} TypioTrayRimeSchemaInfo;

typedef struct TypioTrayRimeMenuInfo {
    bool available;
    char *current_schema;
    char *user_data_dir;
    TypioTrayRimeSchemaInfo schemas[TYPIO_TRAY_RIME_MENU_MAX_SCHEMAS];
    size_t schema_count;
} TypioTrayRimeMenuInfo;

static void free_rime_menu_info(TypioTrayRimeMenuInfo *info) {
    if (!info) {
        return;
    }

    free(info->current_schema);
    free(info->user_data_dir);
    for (size_t i = 0; i < info->schema_count; ++i) {
        free(info->schemas[i].id);
        free(info->schemas[i].name);
    }
    memset(info, 0, sizeof(*info));
}

static char *tray_default_rime_user_dir(TypioTray *tray) {
    const char *data_dir = typio_instance_get_data_dir(tray->instance);
    return data_dir ? typio_path_join(data_dir, "rime") : nullptr;
}

static bool tray_load_rime_menu_info(TypioTray *tray, TypioTrayRimeMenuInfo *info) {
    TypioEngineManager *manager;
    TypioEngine *engine;
    TypioConfig *config;
    TypioRimeSchemaList shared_list;
    char *default_dir;

    if (!tray || !info || !tray->engine_name || strcmp(tray->engine_name, "rime") != 0) {
        return false;
    }

    memset(info, 0, sizeof(*info));
    manager = typio_instance_get_engine_manager(tray->instance);
    engine = manager ? typio_engine_manager_get_active(manager) : nullptr;
    if (!engine || strcmp(typio_engine_get_name(engine), "rime") != 0) {
        return false;
    }

    config = typio_instance_get_engine_config(tray->instance, "rime");
    default_dir = tray_default_rime_user_dir(tray);

    if (!typio_rime_schema_list_load(config, default_dir, &shared_list)) {
        if (config) {
            typio_config_free(config);
        }
        free(default_dir);
        return false;
    }

    info->current_schema = typio_instance_dup_rime_schema(tray->instance);
    if (shared_list.user_data_dir) {
        info->user_data_dir = typio_strdup(shared_list.user_data_dir);
    }
    for (size_t i = 0; i < shared_list.schema_count &&
                        i < TYPIO_TRAY_RIME_MENU_MAX_SCHEMAS; ++i) {
        info->schemas[i].id = shared_list.schemas[i].id
            ? typio_strdup(shared_list.schemas[i].id) : nullptr;
        info->schemas[i].name = shared_list.schemas[i].name
            ? typio_strdup(shared_list.schemas[i].name) : nullptr;
        info->schema_count++;
    }
    info->available = shared_list.available;

    typio_rime_schema_list_clear(&shared_list);
    if (config) {
        typio_config_free(config);
    }
    free(default_dir);
    return info->available;
}

static dbus_bool_t append_icon_pixmap_array(DBusMessageIter *iter,
                                            const char *icon_name) {
    DBusMessageIter array_iter;
    DBusMessageIter struct_iter;
    DBusMessageIter bytes_iter;
    unsigned char *data = nullptr;
    int width = 0;
    int height = 0;
    int data_len = 0;

    if (!typio_tray_icon_pixmap_build(icon_name, 64, &width, &height, &data, &data_len)) {
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "(iiay)", &array_iter)) {
            return FALSE;
        }
        return dbus_message_iter_close_container(iter, &array_iter);
    }

    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "(iiay)", &array_iter)) {
        typio_tray_icon_pixmap_free(data);
        return FALSE;
    }

    if (!dbus_message_iter_open_container(&array_iter, DBUS_TYPE_STRUCT, nullptr, &struct_iter)) {
        typio_tray_icon_pixmap_free(data);
        return FALSE;
    }

    if (!dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_INT32, &width) ||
        !dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_INT32, &height)) {
        typio_tray_icon_pixmap_free(data);
        return FALSE;
    }

    if (!dbus_message_iter_open_container(&struct_iter, DBUS_TYPE_ARRAY, "y", &bytes_iter)) {
        typio_tray_icon_pixmap_free(data);
        return FALSE;
    }

    if (!dbus_message_iter_append_fixed_array(&bytes_iter, DBUS_TYPE_BYTE, &data, data_len)) {
        typio_tray_icon_pixmap_free(data);
        return FALSE;
    }

    if (!dbus_message_iter_close_container(&struct_iter, &bytes_iter) ||
        !dbus_message_iter_close_container(&array_iter, &struct_iter) ||
        !dbus_message_iter_close_container(iter, &array_iter)) {
        typio_tray_icon_pixmap_free(data);
        return FALSE;
    }

    typio_tray_icon_pixmap_free(data);
    return TRUE;
}

static dbus_bool_t append_dict_entry_icon_pixmap(DBusMessageIter *dict,
                                                 const char *key,
                                                 const char *icon_name) {
    DBusMessageIter entry, variant;

    if (!dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry)) {
        return FALSE;
    }

    if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key)) {
        return FALSE;
    }

    if (!dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(iiay)", &variant)) {
        return FALSE;
    }

    if (!append_icon_pixmap_array(&variant, icon_name)) {
        return FALSE;
    }

    if (!dbus_message_iter_close_container(&entry, &variant)) {
        return FALSE;
    }

    if (!dbus_message_iter_close_container(dict, &entry)) {
        return FALSE;
    }

    return TRUE;
}

/* Handle org.freedesktop.DBus.Properties.Get */
static DBusMessage *handle_properties_get(TypioTray *tray, DBusMessage *msg) {
    const char *interface, *property;
    DBusMessage *reply;
    DBusMessageIter iter, variant;

    if (!dbus_message_get_args(msg, nullptr,
                               DBUS_TYPE_STRING, &interface,
                               DBUS_TYPE_STRING, &property,
                               DBUS_TYPE_INVALID)) {
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                      "Invalid arguments");
    }

    reply = dbus_message_new_method_return(msg);
    if (!reply) return nullptr;

    dbus_message_iter_init_append(reply, &iter);

    /* StatusNotifierItem properties */
    if (strcmp(interface, SNI_ITEM_INTERFACE) == 0) {
        if (strcmp(property, "Category") == 0) {
            const char *val = "ApplicationStatus";
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "Id") == 0) {
            const char *val = "typio";
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "Title") == 0) {
            const char *val = tray->title ? tray->title : "Typio";
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "Status") == 0) {
            const char *val;
            switch (tray->status) {
                case TYPIO_TRAY_STATUS_ACTIVE: val = "Active"; break;
                case TYPIO_TRAY_STATUS_NEEDS_ATTENTION: val = "NeedsAttention"; break;
                default: val = "Passive"; break;
            }
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "IconName") == 0) {
            const char *val = tray->icon_name ? tray->icon_name : "typio-keyboard";
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "IconThemePath") == 0) {
            const char *val = tray->icon_theme_path ? tray->icon_theme_path : "";
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "IconPixmap") == 0 ||
                   strcmp(property, "OverlayIconPixmap") == 0 ||
                   strcmp(property, "AttentionIconPixmap") == 0) {
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "a(iiay)", &variant);
            append_icon_pixmap_array(&variant, tray->icon_name);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "OverlayIconName") == 0 ||
                   strcmp(property, "AttentionIconName") == 0) {
            const char *val = "";
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "ToolTip") == 0) {
            /* (sa(iiay)ss) - icon, pixmap, title, description */
            DBusMessageIter st;
            const char *icon = tray->icon_name ? tray->icon_name : "typio-keyboard";
            const char *title = tray->tooltip_title ? tray->tooltip_title : "Typio";
            const char *desc = tray->tooltip_description ? tray->tooltip_description : "";

            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "(sa(iiay)ss)", &variant);
            dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, nullptr, &st);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &icon);
            append_icon_pixmap_array(&st, tray->icon_name);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &title);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &desc);
            dbus_message_iter_close_container(&variant, &st);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "ItemIsMenu") == 0) {
            dbus_bool_t val = FALSE;
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &val);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "Menu") == 0) {
            const char *val = DBUSMENU_PATH;
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "o", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &val);
            dbus_message_iter_close_container(&iter, &variant);
        } else {
            dbus_message_unref(reply);
            return dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY,
                                          "Unknown property");
        }
    }
    /* DBusMenu properties */
    else if (strcmp(interface, DBUSMENU_INTERFACE) == 0) {
        if (strcmp(property, "Version") == 0) {
            dbus_uint32_t val = 3;
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "u", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &val);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "TextDirection") == 0) {
            const char *val = "ltr";
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "Status") == 0) {
            const char *val = "normal";
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
            dbus_message_iter_close_container(&iter, &variant);
        } else if (strcmp(property, "IconThemePath") == 0) {
            DBusMessageIter arr;
            dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "as", &variant);
            dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &arr);
            dbus_message_iter_close_container(&variant, &arr);
            dbus_message_iter_close_container(&iter, &variant);
        } else {
            dbus_message_unref(reply);
            return dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY,
                                          "Unknown property");
        }
    } else {
        dbus_message_unref(reply);
        return dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_INTERFACE,
                                      "Unknown interface");
    }

    return reply;
}

/* Handle org.freedesktop.DBus.Properties.GetAll */
static DBusMessage *handle_properties_getall(TypioTray *tray, DBusMessage *msg) {
    const char *interface;
    DBusMessage *reply;
    DBusMessageIter iter, dict;

    if (!dbus_message_get_args(msg, nullptr,
                               DBUS_TYPE_STRING, &interface,
                               DBUS_TYPE_INVALID)) {
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                      "Invalid arguments");
    }

    reply = dbus_message_new_method_return(msg);
    if (!reply) return nullptr;

    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

    if (strcmp(interface, SNI_ITEM_INTERFACE) == 0) {
        const char *status_str;
        switch (tray->status) {
            case TYPIO_TRAY_STATUS_ACTIVE: status_str = "Active"; break;
            case TYPIO_TRAY_STATUS_NEEDS_ATTENTION: status_str = "NeedsAttention"; break;
            default: status_str = "Passive"; break;
        }

        typio_dbus_append_dict_entry_string(&dict, "Category", "ApplicationStatus");
        typio_dbus_append_dict_entry_string(&dict, "Id", "typio");
        typio_dbus_append_dict_entry_string(&dict, "Title", tray->title ? tray->title : "Typio");
        typio_dbus_append_dict_entry_string(&dict, "Status", status_str);
        typio_dbus_append_dict_entry_string(&dict, "IconName",
                                            tray->icon_name ? tray->icon_name : "typio-keyboard");
        typio_dbus_append_dict_entry_string(&dict, "IconThemePath",
                                            tray->icon_theme_path ? tray->icon_theme_path : "");
        append_dict_entry_icon_pixmap(&dict, "IconPixmap", tray->icon_name);
        typio_dbus_append_dict_entry_string(&dict, "OverlayIconName", "");
        typio_dbus_append_dict_entry_string(&dict, "AttentionIconName", "");
        typio_dbus_append_dict_entry_bool(&dict, "ItemIsMenu", FALSE);
        typio_dbus_append_dict_entry_object_path(&dict, "Menu", DBUSMENU_PATH);
    }

    dbus_message_iter_close_container(&iter, &dict);
    return reply;
}

/* Handle SNI method calls */
static DBusMessage *handle_sni_method(TypioTray *tray, DBusMessage *msg) {
    const char *method = dbus_message_get_member(msg);
    DBusMessage *reply;

    if (strcmp(method, "ContextMenu") == 0 ||
        strcmp(method, "Activate") == 0 ||
        strcmp(method, "SecondaryActivate") == 0) {
        dbus_int32_t x = 0, y = 0;
        bool parsed = false;

        /* Try standard signature (ii) */
        if (dbus_message_get_args(msg, nullptr,
                                  DBUS_TYPE_INT32, &x,
                                  DBUS_TYPE_INT32, &y,
                                  DBUS_TYPE_INVALID)) {
            parsed = true;
        }
        /* Fallback: accept empty arguments (treat as 0,0) */
        else if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_INVALID)) {
            parsed = true;
            x = 0;
            y = 0;
        }

        if (!parsed) {
            return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                          "Invalid arguments");
        }

        typio_log(TYPIO_LOG_DEBUG, "Tray %s at (%d, %d)", method, x, y);

        if (tray->menu_callback) {
            if (strcmp(method, "ContextMenu") == 0) {
                tray->menu_callback(tray, "context_menu", tray->user_data);
            } else if (strcmp(method, "Activate") == 0) {
                tray->menu_callback(tray, "activate", tray->user_data);
            } else {
                tray->menu_callback(tray, "secondary_activate", tray->user_data);
            }
        }
        reply = dbus_message_new_method_return(msg);
    } else if (strcmp(method, "Scroll") == 0) {
        dbus_int32_t delta;
        const char *orientation;
        if (!dbus_message_get_args(msg, nullptr,
                                   DBUS_TYPE_INT32, &delta,
                                   DBUS_TYPE_STRING, &orientation,
                                   DBUS_TYPE_INVALID)) {
            return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                          "Invalid arguments");
        }

        typio_log(TYPIO_LOG_DEBUG, "Tray scroll: delta=%d, orientation=%s",
                  delta, orientation);
        if (tray->menu_callback) {
            tray->menu_callback(tray, delta > 0 ? "scroll_up" : "scroll_down",
                                tray->user_data);
        }
        reply = dbus_message_new_method_return(msg);
    } else {
        reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD,
                                       "Unknown method");
    }

    return reply;
}

/* Build a menu item into the iterator */
static void build_menu_item(DBusMessageIter *parent, int32_t id,
                            const char *label, const char *type,
                            dbus_bool_t enabled) {
    DBusMessageIter var, st, dict, children;

    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "(ia{sv}av)", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_STRUCT, nullptr, &st);

    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &id);

    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "{sv}", &dict);
    if (label) {
        typio_dbus_append_dict_entry_string(&dict, "label", label);
    }
    if (type) {
        typio_dbus_append_dict_entry_string(&dict, "type", type);
    }
    typio_dbus_append_dict_entry_bool(&dict, "enabled", enabled);
    dbus_message_iter_close_container(&st, &dict);

    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "v", &children);
    dbus_message_iter_close_container(&st, &children);

    dbus_message_iter_close_container(&var, &st);
    dbus_message_iter_close_container(parent, &var);
}

static void begin_menu_item(DBusMessageIter *parent, DBusMessageIter *var,
                            DBusMessageIter *st, DBusMessageIter *dict,
                            DBusMessageIter *children, int32_t id,
                            const char *label, dbus_bool_t enabled,
                            dbus_bool_t submenu) {
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "(ia{sv}av)", var);
    dbus_message_iter_open_container(var, DBUS_TYPE_STRUCT, nullptr, st);
    dbus_message_iter_append_basic(st, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(st, DBUS_TYPE_ARRAY, "{sv}", dict);
    if (label) {
        typio_dbus_append_dict_entry_string(dict, "label", label);
    }
    typio_dbus_append_dict_entry_bool(dict, "enabled", enabled);
    if (submenu) {
        typio_dbus_append_dict_entry_string(dict, "children-display", "submenu");
    }
    dbus_message_iter_close_container(st, dict);
    dbus_message_iter_open_container(st, DBUS_TYPE_ARRAY, "v", children);
}

static void end_menu_item(DBusMessageIter *parent, DBusMessageIter *var,
                          DBusMessageIter *st, DBusMessageIter *children) {
    dbus_message_iter_close_container(st, children);
    dbus_message_iter_close_container(var, st);
    dbus_message_iter_close_container(parent, var);
}

static void build_rime_submenu(DBusMessageIter *parent, TypioTray *tray) {
    TypioTrayRimeMenuInfo info;
    DBusMessageIter var, st, dict, children;
    char label[256];

    if (!tray_load_rime_menu_info(tray, &info)) {
        return;
    }

    begin_menu_item(parent, &var, &st, &dict, &children, 20, "Rime", TRUE, TRUE);

    for (size_t i = 0; i < info.schema_count; ++i) {
        bool is_current = info.current_schema &&
                          strcmp(info.current_schema, info.schemas[i].id) == 0;

        if (info.schemas[i].name && info.schemas[i].name[0]) {
            snprintf(label, sizeof(label), "%s %s (%s)",
                     is_current ? "●" : " ", info.schemas[i].name, info.schemas[i].id);
        } else {
            snprintf(label, sizeof(label), "%s %s",
                     is_current ? "●" : " ", info.schemas[i].id);
        }

        build_menu_item(&children, TYPIO_TRAY_RIME_MENU_BASE_ID + (int32_t)i,
                        label, nullptr, TRUE);
    }

    build_menu_item(&children, TYPIO_TRAY_RIME_MENU_RELOAD_ID, "Deploy Rime Config",
                    nullptr, TRUE);
    end_menu_item(parent, &var, &st, &children);
    free_rime_menu_info(&info);
}

/* Handle DBusMenu GetLayout */
static bool parse_menu_getlayout_request(DBusMessage *msg,
                                         dbus_int32_t *parent_id,
                                         dbus_int32_t *depth) {
    DBusMessageIter iter;

    if (!msg || !parent_id || !depth) {
        return false;
    }

    if (!dbus_message_iter_init(msg, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
        return false;
    }
    dbus_message_iter_get_basic(&iter, parent_id);

    if (!dbus_message_iter_next(&iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
        return false;
    }
    dbus_message_iter_get_basic(&iter, depth);
    return true;
}

static DBusMessage *handle_menu_getlayout(TypioTray *tray, DBusMessage *msg) {
    dbus_int32_t parent_id;
    dbus_int32_t depth;
    DBusMessage *reply;
    DBusMessageIter iter, root_st, root_dict, children;

    if (!parse_menu_getlayout_request(msg, &parent_id, &depth)) {
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                      "Invalid arguments");
    }

    reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return nullptr;
    }

    dbus_message_iter_init_append(reply, &iter);

    /* Revision */
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &tray->menu_revision);

    /* Root item: (ia{sv}av) */
    dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, nullptr, &root_st);

    dbus_int32_t root_id = 0;
    dbus_message_iter_append_basic(&root_st, DBUS_TYPE_INT32, &root_id);

    dbus_message_iter_open_container(&root_st, DBUS_TYPE_ARRAY, "{sv}", &root_dict);
    typio_dbus_append_dict_entry_string(&root_dict, "children-display", "submenu");
    dbus_message_iter_close_container(&root_st, &root_dict);

    /* Children */
    dbus_message_iter_open_container(&root_st, DBUS_TYPE_ARRAY, "v", &children);

    int32_t item_id = 1;
    char label[256];

    /* Get available keyboard engines from instance (skip voice engines) */
    TypioEngineManager *manager = typio_instance_get_engine_manager(tray->instance);
    if (manager) {
        size_t engine_count;
        const char **engines = typio_engine_manager_list_ordered_keyboards(manager, &engine_count);
        size_t shown = 0;

        for (size_t i = 0; i < engine_count && i < 10; i++) {
            const TypioEngineInfo *info = typio_engine_manager_get_info(manager, engines[i]);
            if (!info) {
                continue;
            }

            bool is_current = tray->engine_name &&
                              strcmp(engines[i], tray->engine_name) == 0;
            if (is_current) {
                snprintf(label, sizeof(label), "● %s", typio_engine_label_from_info(info));
            } else {
                snprintf(label, sizeof(label), "  %s", typio_engine_label_from_info(info));
            }
            /* Store engine index: IDs 100+ are engine selections */
            build_menu_item(&children, 100 + (int32_t)i, label, nullptr, TRUE);
            shown++;
        }

        if (shown > 0) {
            build_menu_item(&children, item_id++, nullptr, "separator", TRUE);
        }
    }

    if (tray->engine_name && strcmp(tray->engine_name, "rime") == 0) {
        build_rime_submenu(&children, tray);
        build_menu_item(&children, item_id++, nullptr, "separator", TRUE);
    }

    /* Restart / Quit */
    build_menu_item(&children, 98, "Restart", nullptr, TRUE);
    build_menu_item(&children, 99, "Quit", nullptr, TRUE);

    dbus_message_iter_close_container(&root_st, &children);
    dbus_message_iter_close_container(&iter, &root_st);

    return reply;
}

/* Handle DBusMenu Event */
static DBusMessage *handle_menu_event(TypioTray *tray, DBusMessage *msg) {
    dbus_int32_t id;
    const char *event_type;

    DBusMessageIter iter;
    dbus_message_iter_init(msg, &iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32)
        return dbus_message_new_method_return(msg);
    dbus_message_iter_get_basic(&iter, &id);
    dbus_message_iter_next(&iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
        return dbus_message_new_method_return(msg);
    dbus_message_iter_get_basic(&iter, &event_type);

    typio_log(TYPIO_LOG_DEBUG, "Menu event: id=%d, type=%s", id, event_type);

    if (strcmp(event_type, "clicked") == 0) {
        if (id == 98) {
            /* Restart clicked */
            if (tray->menu_callback) {
                tray->menu_callback(tray, "restart", tray->user_data);
            }
        } else if (id == 99) {
            /* Quit clicked */
            if (tray->menu_callback) {
                tray->menu_callback(tray, "quit", tray->user_data);
            }
        } else if (id >= 100 && id < 110) {
            /* Engine selection (IDs 100-109 map to engine index 0-9) */
            int engine_idx = id - 100;
            TypioEngineManager *manager = typio_instance_get_engine_manager(tray->instance);
            if (manager) {
                size_t engine_count;
                const char **engines = typio_engine_manager_list_ordered_keyboards(manager, &engine_count);
                if ((size_t)engine_idx < engine_count && tray->menu_callback) {
                    char action[128];
                    snprintf(action, sizeof(action), "engine:%s", engines[engine_idx]);
                    tray->menu_callback(tray, action, tray->user_data);
                }
            }
        } else if (id >= TYPIO_TRAY_RIME_MENU_BASE_ID &&
                   id < TYPIO_TRAY_RIME_MENU_BASE_ID + TYPIO_TRAY_RIME_MENU_MAX_SCHEMAS) {
            TypioTrayRimeMenuInfo info;
            size_t index = (size_t)(id - TYPIO_TRAY_RIME_MENU_BASE_ID);

            if (tray_load_rime_menu_info(tray, &info)) {
                if (index < info.schema_count && tray->menu_callback) {
                    char action[256];
                    snprintf(action, sizeof(action), "rime-schema:%s", info.schemas[index].id);
                    tray->menu_callback(tray, action, tray->user_data);
                }
                free_rime_menu_info(&info);
            }
        } else if (id == TYPIO_TRAY_RIME_MENU_RELOAD_ID) {
            if (tray->menu_callback) {
                tray->menu_callback(tray, "rime-deploy", tray->user_data);
            }
        }
    }

    return dbus_message_new_method_return(msg);
}

/* Main message handler */
DBusHandlerResult typio_tray_handle_message(DBusConnection *conn,
                                            DBusMessage *msg,
                                            void *user_data) {
    TypioTray *tray = user_data;
    const char *interface = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);
    const char *path = dbus_message_get_path(msg);
    DBusMessage *reply = nullptr;

    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    /* Handle nullptr interface (some panels don't set it) */
    if (!interface) {
        interface = "";
    }

    typio_log(TYPIO_LOG_DEBUG, "D-Bus call: %s.%s on %s", interface, member, path);

    /* Properties interface */
    if (strcmp(interface, DBUS_PROPERTIES_INTERFACE) == 0 ||
        strcmp(member, "Get") == 0 || strcmp(member, "GetAll") == 0) {
        if (strcmp(member, "Get") == 0) {
            reply = handle_properties_get(tray, msg);
        } else if (strcmp(member, "GetAll") == 0) {
            reply = handle_properties_getall(tray, msg);
        }
    }
    /* DBusMenu interface - check by path or interface */
    else if (strcmp(interface, DBUSMENU_INTERFACE) == 0 ||
             strcmp(path, DBUSMENU_PATH) == 0) {
        if (strcmp(member, "GetLayout") == 0) {
            typio_log(TYPIO_LOG_DEBUG, "Menu GetLayout called");
            reply = handle_menu_getlayout(tray, msg);
        } else if (strcmp(member, "Event") == 0) {
            reply = handle_menu_event(tray, msg);
        } else if (strcmp(member, "GetProperty") == 0) {
            reply = dbus_message_new_method_return(msg);
        } else if (strcmp(member, "GetGroupProperties") == 0) {
            /* Return empty array */
            reply = dbus_message_new_method_return(msg);
            DBusMessageIter iter, arr;
            dbus_message_iter_init_append(reply, &iter);
            dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(ia{sv})", &arr);
            dbus_message_iter_close_container(&iter, &arr);
        } else if (strcmp(member, "AboutToShow") == 0) {
            /* Return false (no need to update) */
            reply = dbus_message_new_method_return(msg);
            dbus_bool_t need_update = FALSE;
            dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &need_update, DBUS_TYPE_INVALID);
        } else if (strcmp(member, "Introspect") == 0) {
            /* Introspection for menu path */
            const char *xml =
                "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
                "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
                "<node>\n"
                "  <interface name=\"com.canonical.dbusmenu\">\n"
                "    <method name=\"GetLayout\">\n"
                "      <arg type=\"i\" direction=\"in\"/>\n"
                "      <arg type=\"i\" direction=\"in\"/>\n"
                "      <arg type=\"as\" direction=\"in\"/>\n"
                "      <arg type=\"u\" direction=\"out\"/>\n"
                "      <arg type=\"(ia{sv}av)\" direction=\"out\"/>\n"
                "    </method>\n"
                "    <method name=\"Event\">\n"
                "      <arg type=\"i\" direction=\"in\"/>\n"
                "      <arg type=\"s\" direction=\"in\"/>\n"
                "      <arg type=\"v\" direction=\"in\"/>\n"
                "      <arg type=\"u\" direction=\"in\"/>\n"
                "    </method>\n"
                "    <method name=\"AboutToShow\"><arg type=\"i\" direction=\"in\"/><arg type=\"b\" direction=\"out\"/></method>\n"
                "    <property name=\"Version\" type=\"u\" access=\"read\"/>\n"
                "    <property name=\"Status\" type=\"s\" access=\"read\"/>\n"
                "    <signal name=\"LayoutUpdated\"><arg type=\"u\"/><arg type=\"i\"/></signal>\n"
                "  </interface>\n"
                "</node>\n";
            reply = dbus_message_new_method_return(msg);
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
        }
    }
    /* StatusNotifierItem interface */
    else if (strcmp(interface, SNI_ITEM_INTERFACE) == 0 ||
             strcmp(path, SNI_ITEM_PATH) == 0) {
        reply = handle_sni_method(tray, msg);
    }
    /* Introspection */
    else if (strcmp(member, "Introspect") == 0) {
        const char *xml =
            "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
            "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
            "<node>\n"
            "  <interface name=\"org.kde.StatusNotifierItem\">\n"
            "    <method name=\"ContextMenu\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>\n"
            "    <method name=\"Activate\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>\n"
            "    <method name=\"SecondaryActivate\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>\n"
            "    <method name=\"Scroll\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/></method>\n"
            "    <signal name=\"NewTitle\"/>\n"
            "    <signal name=\"NewIcon\"/>\n"
            "    <signal name=\"NewStatus\"><arg type=\"s\"/></signal>\n"
            "    <signal name=\"NewToolTip\"/>\n"
            "  </interface>\n"
            "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
            "    <method name=\"Get\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"out\"/></method>\n"
            "    <method name=\"GetAll\"><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"out\"/></method>\n"
            "  </interface>\n"
            "</node>\n";
        reply = dbus_message_new_method_return(msg);
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
    }

    if (reply) {
        dbus_connection_send(conn, reply, nullptr);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* Register with StatusNotifierWatcher */
int typio_tray_sni_register(TypioTray *tray) {
    if (!tray || !tray->conn) {
        return -1;
    }

    DBusMessage *msg, *reply;
    DBusError err;

    dbus_error_init(&err);

    msg = dbus_message_new_method_call(SNI_WATCHER_SERVICE,
                                       SNI_WATCHER_PATH,
                                       SNI_WATCHER_INTERFACE,
                                       "RegisterStatusNotifierItem");
    if (!msg) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create registration message");
        return -1;
    }

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &tray->service_name,
                             DBUS_TYPE_INVALID);

    reply = dbus_connection_send_with_reply_and_block(tray->conn, msg, 1000, &err);
    dbus_message_unref(msg);

    if (dbus_error_is_set(&err)) {
        typio_log(TYPIO_LOG_WARNING, "Failed to register with StatusNotifierWatcher: %s",
                  err.message);
        dbus_error_free(&err);
        tray->registered = false;
        return -1;
    }

    if (reply) {
        dbus_message_unref(reply);
    }

    tray->registered = true;
    typio_log(TYPIO_LOG_INFO, "Registered with StatusNotifierWatcher as %s",
              tray->service_name);

    return 0;
}

/* Emit a signal */
void typio_tray_sni_emit_signal(TypioTray *tray, const char *signal_name) {
    if (!tray || !tray->conn || !tray->registered) {
        return;
    }

    DBusMessage *sig = dbus_message_new_signal(SNI_ITEM_PATH,
                                               SNI_ITEM_INTERFACE,
                                               signal_name);
    if (!sig) return;

    /* NewStatus takes a string argument */
    if (strcmp(signal_name, "NewStatus") == 0) {
        const char *status_str;
        switch (tray->status) {
            case TYPIO_TRAY_STATUS_ACTIVE: status_str = "Active"; break;
            case TYPIO_TRAY_STATUS_NEEDS_ATTENTION: status_str = "NeedsAttention"; break;
            default: status_str = "Passive"; break;
        }
        dbus_message_append_args(sig, DBUS_TYPE_STRING, &status_str, DBUS_TYPE_INVALID);
    }

    dbus_connection_send(tray->conn, sig, nullptr);
    dbus_message_unref(sig);
}

void typio_tray_set_status(TypioTray *tray, TypioTrayStatus status) {
    if (!tray || tray->status == status) {
        return;
    }

    tray->status = status;
    typio_tray_sni_emit_signal(tray, "NewStatus");
}

void typio_tray_set_icon(TypioTray *tray, const char *icon_name) {
    if (!tray) {
        return;
    }

    free(tray->icon_name);
    tray->icon_name = icon_name ? typio_strdup(icon_name) : typio_strdup("typio-keyboard");
    typio_tray_sni_emit_signal(tray, "NewIcon");
}

void typio_tray_set_tooltip(TypioTray *tray, const char *title,
                            const char *description) {
    if (!tray) {
        return;
    }

    free(tray->tooltip_title);
    free(tray->tooltip_description);
    tray->tooltip_title = title ? typio_strdup(title) : nullptr;
    tray->tooltip_description = description ? typio_strdup(description) : nullptr;
    typio_tray_sni_emit_signal(tray, "NewToolTip");
}

void typio_tray_update_engine(TypioTray *tray, const char *engine_name,
                              bool is_active) {
    if (!tray) {
        return;
    }

    free(tray->engine_name);
    tray->engine_name = engine_name ? typio_strdup(engine_name) : nullptr;
    tray->engine_active = is_active;

    /* Update menu revision */
    tray->menu_revision++;

    /* Emit menu update signal */
    if (tray->conn && tray->registered) {
        DBusMessage *sig = dbus_message_new_signal(DBUSMENU_PATH,
                                                   DBUSMENU_INTERFACE,
                                                   "LayoutUpdated");
        if (sig) {
            dbus_uint32_t rev = tray->menu_revision;
            dbus_int32_t parent = 0;
            dbus_message_append_args(sig,
                                     DBUS_TYPE_UINT32, &rev,
                                     DBUS_TYPE_INT32, &parent,
                                     DBUS_TYPE_INVALID);
            dbus_connection_send(tray->conn, sig, nullptr);
            dbus_message_unref(sig);
        }
    }

    /* Update tooltip */
    char tooltip[256];
    if (engine_name) {
        snprintf(tooltip, sizeof(tooltip), "Typio - %s%s",
                 engine_name, is_active ? " (active)" : "");
    } else {
        snprintf(tooltip, sizeof(tooltip), "Typio - No engine");
    }
    typio_tray_set_tooltip(tray, tooltip, nullptr);

    /* Update status */
    typio_tray_set_status(tray, is_active ? TYPIO_TRAY_STATUS_ACTIVE
                                          : TYPIO_TRAY_STATUS_PASSIVE);
}

bool typio_tray_is_registered(TypioTray *tray) {
    return tray && tray->registered;
}
