/**
 * @file dbus_helpers.h
 * @brief Shared libdbus container helpers for server-side D-Bus surfaces.
 */

#ifndef TYPIO_SERVER_DBUS_HELPERS_H
#define TYPIO_SERVER_DBUS_HELPERS_H

#include <dbus/dbus.h>

static inline dbus_bool_t typio_dbus_append_dict_entry_string(DBusMessageIter *dict,
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

static inline dbus_bool_t typio_dbus_append_dict_entry_bool(DBusMessageIter *dict,
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

static inline dbus_bool_t typio_dbus_append_dict_entry_int32(DBusMessageIter *dict,
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

static inline dbus_bool_t typio_dbus_append_dict_entry_uint32(DBusMessageIter *dict,
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

static inline dbus_bool_t typio_dbus_append_dict_entry_double(DBusMessageIter *dict,
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

static inline dbus_bool_t typio_dbus_append_dict_entry_object_path(DBusMessageIter *dict,
                                                                   const char *key,
                                                                   const char *value) {
    DBusMessageIter entry;
    DBusMessageIter variant;

    if (!dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key)) {
        return FALSE;
    }
    if (!dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "o", &variant)) {
        return FALSE;
    }
    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &value)) {
        return FALSE;
    }
    if (!dbus_message_iter_close_container(&entry, &variant)) {
        return FALSE;
    }
    return dbus_message_iter_close_container(dict, &entry);
}

#endif
