/**
 * @file main.c
 * @brief typio-client — D-Bus CLI for controlling a running Typio server
 */

#include "client_main.h"
#include "typio/dbus_protocol.h"

#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DBUS_PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"
#define TIMEOUT_MS 3000

/* ------------------------------------------------------------------ */
/*  D-Bus helpers                                                     */
/* ------------------------------------------------------------------ */

static DBusConnection *connect_session_bus(void) {
    DBusError err;
    DBusConnection *conn;

    dbus_error_init(&err);
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "typio-client: D-Bus connection failed: %s\n", err.message);
        dbus_error_free(&err);
        return NULL;
    }
    return conn;
}

static DBusMessage *call_method(DBusConnection *conn,
                                const char *method) {
    DBusMessage *msg;
    DBusMessage *reply;
    DBusError err;

    msg = dbus_message_new_method_call(
        TYPIO_STATUS_DBUS_SERVICE,
        TYPIO_STATUS_DBUS_PATH,
        TYPIO_STATUS_DBUS_INTERFACE,
        method);
    if (!msg) {
        return NULL;
    }

    dbus_error_init(&err);
    reply = dbus_connection_send_with_reply_and_block(conn, msg, TIMEOUT_MS, &err);
    dbus_message_unref(msg);

    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "typio-client: %s failed: %s\n", method, err.message);
        dbus_error_free(&err);
        return NULL;
    }
    return reply;
}

static DBusMessage *call_method_string(DBusConnection *conn,
                                       const char *method,
                                       const char *arg) {
    DBusMessage *msg;
    DBusMessage *reply;
    DBusError err;

    msg = dbus_message_new_method_call(
        TYPIO_STATUS_DBUS_SERVICE,
        TYPIO_STATUS_DBUS_PATH,
        TYPIO_STATUS_DBUS_INTERFACE,
        method);
    if (!msg) {
        return NULL;
    }
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);

    dbus_error_init(&err);
    reply = dbus_connection_send_with_reply_and_block(conn, msg, TIMEOUT_MS, &err);
    dbus_message_unref(msg);

    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "typio-client: %s failed: %s\n", method, err.message);
        dbus_error_free(&err);
        return NULL;
    }
    return reply;
}

static DBusMessage *get_all_properties(DBusConnection *conn) {
    DBusMessage *msg;
    DBusMessage *reply;
    DBusError err;
    const char *iface = TYPIO_STATUS_DBUS_INTERFACE;

    msg = dbus_message_new_method_call(
        TYPIO_STATUS_DBUS_SERVICE,
        TYPIO_STATUS_DBUS_PATH,
        DBUS_PROPERTIES_INTERFACE,
        "GetAll");
    if (!msg) {
        return NULL;
    }
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);

    dbus_error_init(&err);
    reply = dbus_connection_send_with_reply_and_block(conn, msg, TIMEOUT_MS, &err);
    dbus_message_unref(msg);

    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "typio-client: GetAll failed: %s\n", err.message);
        dbus_error_free(&err);
        return NULL;
    }
    return reply;
}

/* ------------------------------------------------------------------ */
/*  Property dict readers                                             */
/* ------------------------------------------------------------------ */

/**
 * Iterate the top-level a{sv} dict returned by GetAll.
 * For each entry, call visitor(key, &variant_iter, user_data).
 */
typedef void (*DictVisitor)(const char *key, DBusMessageIter *variant,
                            void *user_data);

static void walk_dict(DBusMessage *reply, DictVisitor visitor, void *data) {
    DBusMessageIter root;
    DBusMessageIter dict;

    if (!dbus_message_iter_init(reply, &root)) {
        return;
    }
    if (dbus_message_iter_get_arg_type(&root) != DBUS_TYPE_ARRAY) {
        return;
    }
    dbus_message_iter_recurse(&root, &dict);

    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        DBusMessageIter variant;
        const char *key;

        dbus_message_iter_recurse(&dict, &entry);
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        dbus_message_iter_recurse(&entry, &variant);

        visitor(key, &variant, data);
        dbus_message_iter_next(&dict);
    }
}

/* Read a single string value from a variant iterator. */
static const char *variant_string(DBusMessageIter *variant) {
    const char *val;

    if (dbus_message_iter_get_arg_type(variant) != DBUS_TYPE_STRING) {
        return NULL;
    }
    dbus_message_iter_get_basic(variant, &val);
    return val;
}

/* ------------------------------------------------------------------ */
/*  Subcommand: engine                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *active;
    const char *target_prop;
} EngineQueryCtx;

static void engine_active_visitor(const char *key, DBusMessageIter *variant,
                                  void *user_data) {
    EngineQueryCtx *ctx = user_data;

    if (strcmp(key, ctx->target_prop) == 0) {
        ctx->active = variant_string(variant);
    }
}

typedef struct {
    const char *active;
} EngineListCtx;

static void engine_list_visitor(const char *key, DBusMessageIter *variant,
                                void *user_data) {
    EngineListCtx *ctx = user_data;

    if (strcmp(key, TYPIO_STATUS_PROP_ACTIVE_KEYBOARD_ENGINE) == 0) {
        ctx->active = variant_string(variant);
    } else if (strcmp(key, TYPIO_STATUS_PROP_ORDERED_KEYBOARD_ENGINES) == 0) {
        DBusMessageIter array;

        if (dbus_message_iter_get_arg_type(variant) != DBUS_TYPE_ARRAY) {
            return;
        }
        dbus_message_iter_recurse(variant, &array);
        while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
            const char *name;
            dbus_message_iter_get_basic(&array, &name);
            if (ctx->active && strcmp(name, ctx->active) == 0) {
                printf("* %s\n", name);
            } else {
                printf("  %s\n", name);
            }
            dbus_message_iter_next(&array);
        }
    }
}

static int cmd_engine(DBusConnection *conn, int argc, char **argv) {
    if (argc < 1) {
        /* No subcommand: print active keyboard engine. */
        DBusMessage *reply = get_all_properties(conn);
        if (!reply) {
            return 1;
        }
        EngineQueryCtx ctx = { .target_prop = TYPIO_STATUS_PROP_ACTIVE_KEYBOARD_ENGINE };
        walk_dict(reply, engine_active_visitor, &ctx);
        if (ctx.active) {
            printf("%s\n", ctx.active);
        }
        dbus_message_unref(reply);
        return 0;
    }

    const char *sub = argv[0];

    if (strcmp(sub, "list") == 0) {
        /* Two-pass: first collect active, then print list with marker.
         * We do it in one pass by buffering during walk. */
        DBusMessage *reply = get_all_properties(conn);
        if (!reply) {
            return 1;
        }
        EngineListCtx ctx = {0};
        /* First pass: get active engine. */
        walk_dict(reply, engine_active_visitor,
                  &(EngineQueryCtx){ .target_prop = TYPIO_STATUS_PROP_ACTIVE_KEYBOARD_ENGINE });
        /* Need to re-walk with the active name. Get it first. */
        EngineQueryCtx qctx = { .target_prop = TYPIO_STATUS_PROP_ACTIVE_KEYBOARD_ENGINE };
        walk_dict(reply, engine_active_visitor, &qctx);
        ctx.active = qctx.active;
        walk_dict(reply, engine_list_visitor, &ctx);
        dbus_message_unref(reply);
        return 0;
    }

    if (strcmp(sub, "next") == 0) {
        DBusMessage *reply = call_method(conn, TYPIO_STATUS_METHOD_NEXT_ENGINE);
        if (!reply) {
            return 1;
        }
        dbus_message_unref(reply);
        return 0;
    }

    /* Otherwise treat as engine name to activate. */
    DBusMessage *reply = call_method_string(conn,
                                            TYPIO_STATUS_METHOD_ACTIVATE_ENGINE,
                                            sub);
    if (!reply) {
        return 1;
    }
    dbus_message_unref(reply);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Subcommand: rime                                                  */
/* ------------------------------------------------------------------ */

static int cmd_rime_schema(DBusConnection *conn, int argc, char **argv) {
    if (argc < 1) {
        /* Print current Rime schema. */
        DBusMessage *reply = get_all_properties(conn);
        if (!reply) {
            return 1;
        }
        EngineQueryCtx ctx = { .target_prop = TYPIO_STATUS_PROP_RIME_SCHEMA };
        walk_dict(reply, engine_active_visitor, &ctx);
        if (ctx.active && *ctx.active) {
            printf("%s\n", ctx.active);
        }
        dbus_message_unref(reply);
        return 0;
    }

    DBusMessage *reply = call_method_string(conn,
                                            TYPIO_STATUS_METHOD_SET_RIME_SCHEMA,
                                            argv[0]);
    if (!reply) {
        return 1;
    }
    dbus_message_unref(reply);
    return 0;
}

static int cmd_rime(DBusConnection *conn, int argc, char **argv) {
    const char *sub;
    DBusMessage *reply;

    if (argc < 1) {
        return cmd_rime_schema(conn, 0, nullptr);
    }

    sub = argv[0];
    if (strcmp(sub, "schema") == 0) {
        return cmd_rime_schema(conn, argc - 1, argv + 1);
    }

    if (strcmp(sub, "deploy") == 0) {
        reply = call_method(conn, TYPIO_STATUS_METHOD_DEPLOY_RIME_CONFIG);
        if (!reply) {
            return 1;
        }
        dbus_message_unref(reply);
        return 0;
    }

    /* Keep `typio-client rime NAME` as a short form for schema switching. */
    return cmd_rime_schema(conn, argc, argv);
}

/* ------------------------------------------------------------------ */
/*  Subcommand: config                                                */
/* ------------------------------------------------------------------ */

static int cmd_config(DBusConnection *conn, int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: typio-client config <reload|get|set TEXT>\n");
        return 1;
    }

    const char *sub = argv[0];

    if (strcmp(sub, "reload") == 0) {
        DBusMessage *reply = call_method(conn, TYPIO_STATUS_METHOD_RELOAD_CONFIG);
        if (!reply) {
            return 1;
        }
        dbus_message_unref(reply);
        return 0;
    }

    if (strcmp(sub, "get") == 0) {
        DBusMessage *reply = get_all_properties(conn);
        if (!reply) {
            return 1;
        }
        EngineQueryCtx ctx = { .target_prop = TYPIO_STATUS_PROP_CONFIG_TEXT };
        walk_dict(reply, engine_active_visitor, &ctx);
        if (ctx.active) {
            printf("%s", ctx.active);
        }
        dbus_message_unref(reply);
        return 0;
    }

    if (strcmp(sub, "set") == 0) {
        if (argc < 2) {
            fprintf(stderr, "typio-client: config set expects a text argument\n");
            return 1;
        }
        DBusMessage *reply = call_method_string(conn,
                                                TYPIO_STATUS_METHOD_SET_CONFIG_TEXT,
                                                argv[1]);
        if (!reply) {
            return 1;
        }
        dbus_message_unref(reply);
        return 0;
    }

    fprintf(stderr, "typio-client: unknown config subcommand: %s\n", sub);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Subcommand: status                                                */
/* ------------------------------------------------------------------ */

static void status_visitor(const char *key, DBusMessageIter *variant,
                           void *user_data) {
    (void) user_data;

    if (strcmp(key, TYPIO_STATUS_PROP_VERSION) == 0 ||
        strcmp(key, TYPIO_STATUS_PROP_ACTIVE_KEYBOARD_ENGINE) == 0 ||
        strcmp(key, TYPIO_STATUS_PROP_ACTIVE_VOICE_ENGINE) == 0 ||
        strcmp(key, TYPIO_STATUS_PROP_RIME_SCHEMA) == 0) {
        const char *val = variant_string(variant);
        if (val && *val) {
            printf("%-26s %s\n", key, val);
        }
    } else if (strcmp(key, TYPIO_STATUS_PROP_ORDERED_KEYBOARD_ENGINES) == 0 ||
               strcmp(key, TYPIO_STATUS_PROP_AVAILABLE_VOICE_ENGINES) == 0) {
        printf("%-26s", key);
        DBusMessageIter array;
        if (dbus_message_iter_get_arg_type(variant) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(variant, &array);
            int first = 1;
            while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
                const char *val;
                dbus_message_iter_get_basic(&array, &val);
                printf("%s%s", first ? " " : ", ", val);
                first = 0;
                dbus_message_iter_next(&array);
            }
        }
        printf("\n");
    }
}

static int cmd_status(DBusConnection *conn) {
    DBusMessage *reply = get_all_properties(conn);
    if (!reply) {
        return 1;
    }
    walk_dict(reply, status_visitor, NULL);
    dbus_message_unref(reply);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Subcommand: stop                                                  */
/* ------------------------------------------------------------------ */

static int cmd_stop(DBusConnection *conn) {
    DBusMessage *reply = call_method(conn, TYPIO_STATUS_METHOD_STOP);
    if (!reply) {
        return 1;
    }
    dbus_message_unref(reply);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Subcommand: version                                               */
/* ------------------------------------------------------------------ */

static int cmd_version(DBusConnection *conn) {
    DBusMessage *reply = get_all_properties(conn);
    if (!reply) {
        return 1;
    }
    EngineQueryCtx ctx = { .target_prop = TYPIO_STATUS_PROP_VERSION };
    walk_dict(reply, engine_active_visitor, &ctx);
    if (ctx.active) {
        printf("%s\n", ctx.active);
    }
    dbus_message_unref(reply);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

static void print_help(const char *prog) {
    printf("Usage: %s <command> [args...]\n\n", prog);
    printf("Commands:\n");
    printf("  daemon                   Start the Typio background service\n");
    printf("  engine [list|next|NAME]  Query or switch keyboard engine\n");
    printf("  rime [schema|deploy]     Query, deploy, or set Rime schema\n");
    printf("  config <reload|get|set>  Manage configuration\n");
    printf("  status                   Show server status\n");
    printf("  stop                     Stop the Typio server\n");
    printf("  version                  Show server version\n");
    printf("  help                     Show this help message\n");
}

int client_main(int argc, char *argv[]) {
    DBusConnection *conn;
    int rc;

    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 ||
        strcmp(cmd, "-h") == 0) {
        print_help(argv[0]);
        return 0;
    }

    conn = connect_session_bus();
    if (!conn) {
        return 1;
    }

    if (strcmp(cmd, "engine") == 0) {
        rc = cmd_engine(conn, argc - 2, argv + 2);
    } else if (strcmp(cmd, "rime") == 0) {
        rc = cmd_rime(conn, argc - 2, argv + 2);
    } else if (strcmp(cmd, "schema") == 0) {
        rc = cmd_rime_schema(conn, argc - 2, argv + 2);
    } else if (strcmp(cmd, "config") == 0) {
        rc = cmd_config(conn, argc - 2, argv + 2);
    } else if (strcmp(cmd, "status") == 0) {
        rc = cmd_status(conn);
    } else if (strcmp(cmd, "stop") == 0) {
        rc = cmd_stop(conn);
    } else if (strcmp(cmd, "version") == 0) {
        rc = cmd_version(conn);
    } else {
        fprintf(stderr, "typio-client: unknown command: %s\n", cmd);
        print_help(argv[0]);
        rc = 1;
    }

    dbus_connection_unref(conn);
    return rc;
}
