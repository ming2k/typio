/**
 * @file test_status_bus.c
 * @brief D-Bus status interface integration tests
 */


#include "status/status.h"
#include "typio/dbus_protocol.h"
#include "typio/typio.h"

#include <dbus/dbus.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  Running %s... ", #name); \
        tests_run++; \
        test_##name(); \
        tests_passed++; \
        printf("OK\n"); \
    } \
    static void test_##name(void)

#define ASSERT(expr) \
    do { \
        if (!(expr)) { \
            printf("FAILED\n"); \
            printf("    Assertion failed: %s\n", #expr); \
            printf("    At %s:%d\n", __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)

typedef struct TestBusProcess {
    pid_t pid;
    char address[1024];
} TestBusProcess;

typedef struct TestRuntimeStateFixture {
    const char *backend;
    const char *phase;
    const char *vk_state;
    uint32_t active_key_generation;
    uint32_t keymap_generation;
} TestRuntimeStateFixture;

static void sleep_briefly(void) {
    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 1000 * 1000,
    };

    nanosleep(&delay, nullptr);
}

static void read_line_or_die(int fd, char *buf, size_t size) {
    size_t i = 0;
    char ch;

    ASSERT(buf && size > 1);

    while (i + 1 < size) {
        ssize_t n = read(fd, &ch, 1);
        ASSERT(n == 1);
        if (ch == '\n') {
            break;
        }
        buf[i++] = ch;
    }
    buf[i] = '\0';
}

static TestBusProcess start_test_bus(void) {
    int pipefd[2];
    pid_t pid;
    TestBusProcess bus = {};
    char pid_line[64];

    ASSERT(pipe(pipefd) == 0);

    pid = fork();
    ASSERT(pid >= 0);

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execlp("dbus-daemon", "dbus-daemon",
               "--session", "--nofork",
               "--print-address=1", "--print-pid=1",
               (char *)nullptr);
        _exit(127);
    }

    close(pipefd[1]);
    read_line_or_die(pipefd[0], bus.address, sizeof(bus.address));
    read_line_or_die(pipefd[0], pid_line, sizeof(pid_line));
    close(pipefd[0]);

    bus.pid = (pid_t)atoi(pid_line);
    ASSERT(bus.pid > 0);
    ASSERT(setenv("DBUS_SESSION_BUS_ADDRESS", bus.address, 1) == 0);
    return bus;
}

static void stop_test_bus(TestBusProcess *bus) {
    int status;

    if (!bus || bus->pid <= 0) {
        return;
    }

    kill(bus->pid, SIGTERM);
    waitpid(bus->pid, &status, 0);
    bus->pid = 0;
}

static DBusConnection *open_client_connection(const char *address) {
    DBusConnection *conn;
    DBusError err;

    dbus_error_init(&err);
    conn = dbus_connection_open_private(address, &err);
    ASSERT(conn != nullptr);
    ASSERT(!dbus_error_is_set(&err));
    ASSERT(dbus_bus_register(conn, &err));
    ASSERT(!dbus_error_is_set(&err));
    return conn;
}

static DBusMessage *call_get_all(DBusConnection *client,
                                 TypioStatusBus *bus) {
    DBusMessage *msg;
    DBusPendingCall *pending = nullptr;
    DBusMessage *reply = nullptr;
    const char *interface = TYPIO_STATUS_DBUS_INTERFACE;

    msg = dbus_message_new_method_call(TYPIO_STATUS_DBUS_SERVICE,
                                       TYPIO_STATUS_DBUS_PATH,
                                       "org.freedesktop.DBus.Properties",
                                       "GetAll");
    ASSERT(msg != nullptr);
    ASSERT(dbus_message_append_args(msg,
                                    DBUS_TYPE_STRING, &interface,
                                    DBUS_TYPE_INVALID));
    ASSERT(dbus_connection_send_with_reply(client, msg, &pending, 1000));
    ASSERT(pending != nullptr);
    dbus_message_unref(msg);

    for (int i = 0; i < 100 && !dbus_pending_call_get_completed(pending); ++i) {
        typio_status_bus_dispatch(bus);
        dbus_connection_read_write_dispatch(client, 10);
        sleep_briefly();
    }

    ASSERT(dbus_pending_call_get_completed(pending));
    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    ASSERT(reply != nullptr);
    return reply;
}

static DBusMessage *call_status_method(DBusConnection *client,
                                       TypioStatusBus *bus,
                                       const char *method,
                                       const char *string_arg) {
    DBusMessage *msg;
    DBusPendingCall *pending = nullptr;
    DBusMessage *reply = nullptr;

    msg = dbus_message_new_method_call(TYPIO_STATUS_DBUS_SERVICE,
                                       TYPIO_STATUS_DBUS_PATH,
                                       TYPIO_STATUS_DBUS_INTERFACE,
                                       method);
    ASSERT(msg != nullptr);
    if (string_arg) {
        ASSERT(dbus_message_append_args(msg,
                                        DBUS_TYPE_STRING, &string_arg,
                                        DBUS_TYPE_INVALID));
    }
    ASSERT(dbus_connection_send_with_reply(client, msg, &pending, 1000));
    ASSERT(pending != nullptr);
    dbus_message_unref(msg);

    for (int i = 0; i < 100 && !dbus_pending_call_get_completed(pending); ++i) {
        typio_status_bus_dispatch(bus);
        dbus_connection_read_write_dispatch(client, 10);
        sleep_briefly();
    }

    ASSERT(dbus_pending_call_get_completed(pending));
    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    ASSERT(reply != nullptr);
    return reply;
}

static bool dict_contains_string(DBusMessageIter *dict,
                                 const char *key,
                                 const char *expected) {
    DBusMessageIter entry;

    while (dbus_message_iter_get_arg_type(dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter kv;
        DBusMessageIter variant;
        const char *entry_key = nullptr;
        const char *entry_val = nullptr;

        dbus_message_iter_recurse(dict, &entry);
        kv = entry;
        if (dbus_message_iter_get_arg_type(&kv) != DBUS_TYPE_STRING) {
            dbus_message_iter_next(dict);
            continue;
        }

        dbus_message_iter_get_basic(&kv, &entry_key);
        dbus_message_iter_next(&kv);
        if (dbus_message_iter_get_arg_type(&kv) != DBUS_TYPE_VARIANT) {
            dbus_message_iter_next(dict);
            continue;
        }

        dbus_message_iter_recurse(&kv, &variant);
        if (strcmp(entry_key, key) == 0 &&
            dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&variant, &entry_val);
            return strcmp(entry_val, expected) == 0;
        }

        dbus_message_iter_next(dict);
    }

    return false;
}

static bool dict_contains_uint32(DBusMessageIter *dict,
                                   const char *key,
                                   uint32_t expected) {
    DBusMessageIter entry;

    while (dbus_message_iter_get_arg_type(dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter kv;
        DBusMessageIter variant;
        const char *entry_key = nullptr;
        dbus_uint32_t entry_val = 0;

        dbus_message_iter_recurse(dict, &entry);
        kv = entry;
        if (dbus_message_iter_get_arg_type(&kv) != DBUS_TYPE_STRING) {
            dbus_message_iter_next(dict);
            continue;
        }

        dbus_message_iter_get_basic(&kv, &entry_key);
        dbus_message_iter_next(&kv);
        if (dbus_message_iter_get_arg_type(&kv) != DBUS_TYPE_VARIANT) {
            dbus_message_iter_next(dict);
            continue;
        }

        dbus_message_iter_recurse(&kv, &variant);
        if (strcmp(entry_key, key) == 0 &&
            dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_UINT32) {
            dbus_message_iter_get_basic(&variant, &entry_val);
            return entry_val == expected;
        }

        dbus_message_iter_next(dict);
    }

    return false;
}

static bool dict_contains_string_dict_entry(DBusMessageIter *dict,
                                            const char *key,
                                            const char *entry_key,
                                            const char *expected) {
    DBusMessageIter entry;

    while (dbus_message_iter_get_arg_type(dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter kv;
        DBusMessageIter variant;
        DBusMessageIter map;
        const char *outer_key = nullptr;

        dbus_message_iter_recurse(dict, &entry);
        kv = entry;
        if (dbus_message_iter_get_arg_type(&kv) != DBUS_TYPE_STRING) {
            dbus_message_iter_next(dict);
            continue;
        }

        dbus_message_iter_get_basic(&kv, &outer_key);
        dbus_message_iter_next(&kv);
        if (dbus_message_iter_get_arg_type(&kv) != DBUS_TYPE_VARIANT) {
            dbus_message_iter_next(dict);
            continue;
        }

        dbus_message_iter_recurse(&kv, &variant);
        if (strcmp(outer_key, key) == 0 &&
            dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&variant, &map);
            while (dbus_message_iter_get_arg_type(&map) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter map_entry;
                DBusMessageIter map_kv;
                DBusMessageIter nested;
                const char *map_key = nullptr;
                const char *map_value = nullptr;

                dbus_message_iter_recurse(&map, &map_entry);
                map_kv = map_entry;
                dbus_message_iter_get_basic(&map_kv, &map_key);
                dbus_message_iter_next(&map_kv);
                if (dbus_message_iter_get_arg_type(&map_kv) == DBUS_TYPE_VARIANT) {
                    dbus_message_iter_recurse(&map_kv, &nested);
                    if (dbus_message_iter_get_arg_type(&nested) == DBUS_TYPE_STRING) {
                        dbus_message_iter_get_basic(&nested, &map_value);
                    }
                } else if (dbus_message_iter_get_arg_type(&map_kv) == DBUS_TYPE_STRING) {
                    dbus_message_iter_get_basic(&map_kv, &map_value);
                }
                if (strcmp(map_key, entry_key) == 0) {
                    return map_value && strcmp(map_value, expected) == 0;
                }
                dbus_message_iter_next(&map);
            }
            return false;
        }

        dbus_message_iter_next(dict);
    }

    return false;
}

static bool reply_contains_active_engine(DBusMessage *reply,
                                         const char *expected_engine) {
    DBusMessageIter iter;
    DBusMessageIter dict;

    ASSERT(dbus_message_iter_init(reply, &iter));
    ASSERT(dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY);
    dbus_message_iter_recurse(&iter, &dict);
    return dict_contains_string(&dict, TYPIO_STATUS_PROP_ACTIVE_ENGINE, expected_engine);
}

static bool reply_contains_active_voice_engine(DBusMessage *reply,
                                               const char *expected_engine) {
    DBusMessageIter iter;
    DBusMessageIter dict;

    ASSERT(dbus_message_iter_init(reply, &iter));
    ASSERT(dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY);
    dbus_message_iter_recurse(&iter, &dict);
    return dict_contains_string(&dict, TYPIO_STATUS_PROP_ACTIVE_VOICE_ENGINE, expected_engine);
}

static bool dict_contains_string_array(DBusMessageIter *dict,
                                       const char *key,
                                       const char *const *expected,
                                       size_t expected_count) {
    DBusMessageIter entry;

    while (dbus_message_iter_get_arg_type(dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter kv;
        DBusMessageIter variant;
        DBusMessageIter array;
        const char *entry_key = nullptr;
        size_t index = 0;

        dbus_message_iter_recurse(dict, &entry);
        kv = entry;
        if (dbus_message_iter_get_arg_type(&kv) != DBUS_TYPE_STRING) {
            dbus_message_iter_next(dict);
            continue;
        }

        dbus_message_iter_get_basic(&kv, &entry_key);
        dbus_message_iter_next(&kv);
        if (dbus_message_iter_get_arg_type(&kv) != DBUS_TYPE_VARIANT) {
            dbus_message_iter_next(dict);
            continue;
        }

        dbus_message_iter_recurse(&kv, &variant);
        if (strcmp(entry_key, key) == 0 &&
            dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&variant, &array);
            while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
                const char *value = nullptr;
                if (index >= expected_count) {
                    return false;
                }
                dbus_message_iter_get_basic(&array, &value);
                if (strcmp(value, expected[index]) != 0) {
                    return false;
                }
                index++;
                dbus_message_iter_next(&array);
            }
            return index == expected_count;
        }

        dbus_message_iter_next(dict);
    }

    return false;
}

static bool reply_contains_string_array(DBusMessage *reply,
                                        const char *key,
                                        const char *const *expected,
                                        size_t expected_count) {
    DBusMessageIter iter;
    DBusMessageIter dict;

    ASSERT(dbus_message_iter_init(reply, &iter));
    ASSERT(dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY);
    dbus_message_iter_recurse(&iter, &dict);
    return dict_contains_string_array(&dict, key, expected, expected_count);
}

static bool reply_contains_string_dict_entry(DBusMessage *reply,
                                             const char *key,
                                             const char *entry_key,
                                             const char *expected) {
    DBusMessageIter iter;
    DBusMessageIter dict;

    ASSERT(dbus_message_iter_init(reply, &iter));
    ASSERT(dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY);
    dbus_message_iter_recurse(&iter, &dict);
    return dict_contains_string_dict_entry(&dict, key, entry_key, expected);
}

static bool reply_contains_uint32_dict_entry(DBusMessage *reply,
                                             const char *key,
                                             const char *entry_key,
                                             uint32_t expected) {
    DBusMessageIter iter;
    DBusMessageIter dict;
    DBusMessageIter entry;

    ASSERT(dbus_message_iter_init(reply, &iter));
    ASSERT(dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY);
    dbus_message_iter_recurse(&iter, &dict);

    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter kv;
        DBusMessageIter variant;
        DBusMessageIter state_dict;
        const char *outer_key = nullptr;

        dbus_message_iter_recurse(&dict, &entry);
        kv = entry;
        dbus_message_iter_get_basic(&kv, &outer_key);
        dbus_message_iter_next(&kv);
        dbus_message_iter_recurse(&kv, &variant);

        if (strcmp(outer_key, key) == 0 &&
            dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&variant, &state_dict);
            return dict_contains_uint32(&state_dict, entry_key, expected);
        }

        dbus_message_iter_next(&dict);
    }

    return false;
}

static TypioResult mock_init(TypioEngine *engine, [[maybe_unused]] TypioInstance *instance) {
    engine->active = true;
    return TYPIO_OK;
}

static void mock_destroy([[maybe_unused]] TypioEngine *engine) {
}

static TypioKeyProcessResult mock_process_key([[maybe_unused]] TypioEngine *engine,
                                              [[maybe_unused]] TypioInputContext *ctx,
                                              [[maybe_unused]] const TypioKeyEvent *event) {
    return TYPIO_KEY_NOT_HANDLED;
}

static const TypioEngineInfo mock_voice_info = {
    .name = "mock-voice",
    .display_name = "Mock Voice Engine",
    .description = "A mock voice engine for testing",
    .version = "1.0.0",
    .author = "Test",
    .icon = NULL,
    .language = NULL,
    .type = TYPIO_ENGINE_TYPE_VOICE,
    .capabilities = TYPIO_CAP_VOICE_INPUT,
    .api_version = TYPIO_API_VERSION,
};

static const TypioEngineBaseOps mock_voice_base_ops = {
    .init = mock_init,
    .destroy = mock_destroy,
    .focus_in = NULL,
    .focus_out = NULL,
    .reset = NULL,
    .reload_config = NULL,
};

static const TypioEngineInfo *mock_voice_get_info(void) {
    return &mock_voice_info;
}

static char *mock_voice_process_audio([[maybe_unused]] TypioEngine *engine,
                                       [[maybe_unused]] const float *samples,
                                       [[maybe_unused]] size_t n_samples) {
    return NULL;
}

static const TypioVoiceEngineOps mock_voice_ops = {
    .process_audio = mock_voice_process_audio,
};

static TypioEngine *mock_voice_create(void) {
    return typio_engine_new(&mock_voice_info, &mock_voice_base_ops, NULL,
                            &mock_voice_ops);
}

static void mock_runtime_state_callback(void *user_data,
                                        TypioStatusRuntimeState *state) {
    TestRuntimeStateFixture *fixture = user_data;

    ASSERT(state != nullptr);
    ASSERT(fixture != nullptr);

    state->frontend_backend = fixture->backend;
    state->lifecycle_phase = fixture->phase;
    state->virtual_keyboard_state = fixture->vk_state;
    state->keyboard_grab_active = true;
    state->virtual_keyboard_has_keymap = true;
    state->watchdog_armed = true;
    state->active_key_generation = fixture->active_key_generation;
    state->virtual_keyboard_keymap_generation = fixture->keymap_generation;
    state->virtual_keyboard_drop_count = 7;
    state->virtual_keyboard_state_age_ms = 42;
    state->virtual_keyboard_keymap_age_ms = 5;
    state->virtual_keyboard_forward_age_ms = 3;
    state->virtual_keyboard_keymap_deadline_remaining_ms = 900;
}

static bool reply_contains_engine_state_name(DBusMessage *reply,
                                             const char *expected_engine) {
    DBusMessageIter iter;
    DBusMessageIter dict;

    ASSERT(dbus_message_iter_init(reply, &iter));
    ASSERT(dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY);
    dbus_message_iter_recurse(&iter, &dict);

    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        DBusMessageIter kv;
        DBusMessageIter variant;
        DBusMessageIter state_dict;
        const char *entry_key = nullptr;

        dbus_message_iter_recurse(&dict, &entry);
        kv = entry;
        dbus_message_iter_get_basic(&kv, &entry_key);
        dbus_message_iter_next(&kv);
        dbus_message_iter_recurse(&kv, &variant);

        if (strcmp(entry_key, TYPIO_STATUS_PROP_ACTIVE_ENGINE_STATE) == 0 &&
            dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&variant, &state_dict);
            return dict_contains_string(&state_dict, "name", expected_engine);
        }

        dbus_message_iter_next(&dict);
    }

    return false;
}

TEST(exports_basic_engine_state_and_emits_change_signal) {
    TestBusProcess bus_proc;
    TypioInstanceConfig config = { .default_engine = "basic" };
    TypioInstance *instance;
    TypioStatusBus *bus;
    DBusConnection *client;
    DBusMessage *reply;
    DBusError err;
    DBusMessage *signal_msg = nullptr;
    TestRuntimeStateFixture runtime_state = {
        .backend = "wayland",
        .phase = "active",
        .vk_state = "ready",
        .active_key_generation = 11,
        .keymap_generation = 11,
    };

    bus_proc = start_test_bus();
    instance = typio_instance_new_with_config(&config);
    ASSERT(instance != nullptr);
    ASSERT(typio_instance_init(instance) == TYPIO_OK);
    {
        TypioConfig *instance_config = typio_instance_get_config(instance);
        const char *order[] = {"basic", "mock-voice"};
        ASSERT(instance_config != NULL);
        ASSERT(typio_config_set_string_array(instance_config, "engine_order", order, 2) == TYPIO_OK);
    }
    ASSERT(typio_engine_manager_register(typio_instance_get_engine_manager(instance),
                                         mock_voice_create,
                                         mock_voice_get_info) == TYPIO_OK);
    ASSERT(typio_engine_manager_set_active_voice(typio_instance_get_engine_manager(instance),
                                                 "mock-voice") == TYPIO_OK);

    bus = typio_status_bus_new(instance);
    ASSERT(bus != nullptr);
    typio_status_bus_set_runtime_state_callback(bus,
                                                mock_runtime_state_callback,
                                                &runtime_state);

    client = open_client_connection(bus_proc.address);
    reply = call_get_all(client, bus);
    ASSERT(reply_contains_active_engine(reply, "basic"));
    ASSERT(reply_contains_active_voice_engine(reply, "mock-voice"));
    ASSERT(reply_contains_engine_state_name(reply, "basic"));
    {
        const char *ordered[] = {"basic"};
        const char *engine_order[] = {"basic", "mock-voice"};
        ASSERT(reply_contains_string_array(reply, TYPIO_STATUS_PROP_ORDERED_ENGINES,
                                           ordered, 1));
        ASSERT(reply_contains_string_array(reply, TYPIO_STATUS_PROP_ENGINE_ORDER,
                                           engine_order, 2));
        ASSERT(reply_contains_string_dict_entry(reply, TYPIO_STATUS_PROP_ENGINE_DISPLAY_NAMES,
                                                "basic", "Basic"));
    }
    ASSERT(reply_contains_string_dict_entry(reply, TYPIO_STATUS_PROP_RUNTIME_STATE,
                                            "frontend_backend", "wayland"));
    ASSERT(reply_contains_string_dict_entry(reply, TYPIO_STATUS_PROP_RUNTIME_STATE,
                                            "lifecycle_phase", "active"));
    ASSERT(reply_contains_string_dict_entry(reply, TYPIO_STATUS_PROP_RUNTIME_STATE,
                                            "virtual_keyboard_state", "ready"));
    ASSERT(reply_contains_uint32_dict_entry(reply, TYPIO_STATUS_PROP_RUNTIME_STATE,
                                            "active_key_generation", 11));
    ASSERT(reply_contains_uint32_dict_entry(reply, TYPIO_STATUS_PROP_RUNTIME_STATE,
                                            "virtual_keyboard_keymap_generation", 11));
    dbus_message_unref(reply);

    reply = call_status_method(client, bus, TYPIO_STATUS_METHOD_ACTIVATE_ENGINE, "basic");
    dbus_message_unref(reply);

    reply = call_status_method(client, bus, TYPIO_STATUS_METHOD_RELOAD_CONFIG, nullptr);
    dbus_message_unref(reply);

    dbus_error_init(&err);
    dbus_bus_add_match(client,
                       "type='signal',interface='org.freedesktop.DBus.Properties',"
                       "path='/org/typio/InputMethod1'",
                       &err);
    ASSERT(!dbus_error_is_set(&err));
    dbus_connection_flush(client);

    typio_status_bus_emit_properties_changed(bus);
    for (int i = 0; i < 100 && signal_msg == nullptr; ++i) {
        typio_status_bus_dispatch(bus);
        dbus_connection_read_write(client, 10);
        signal_msg = dbus_connection_pop_message(client);
        sleep_briefly();
    }

    ASSERT(signal_msg != nullptr);
    ASSERT(dbus_message_is_signal(signal_msg,
                                  "org.freedesktop.DBus.Properties",
                                  "PropertiesChanged"));
    dbus_message_unref(signal_msg);

    dbus_connection_close(client);
    dbus_connection_unref(client);
    typio_status_bus_destroy(bus);
    typio_instance_free(instance);
    stop_test_bus(&bus_proc);
}

int main(void) {
    printf("Running status bus tests:\n");
    run_test_exports_basic_engine_state_and_emits_change_signal();
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
