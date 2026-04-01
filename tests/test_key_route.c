/**
 * @file test_key_route.c
 * @brief Tests for basic-engine key routing bypass
 */

#include "key_route.h"
#include "wl_frontend_internal.h"
#include "typio/event.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct RecordedVkEvent {
    uint32_t time;
    uint32_t key;
    uint32_t state;
    uint32_t unicode;
} RecordedVkEvent;

static const char *g_active_engine_name;
static const char *g_basic_printable_key_mode;
static bool g_process_key_result;
static int g_process_key_calls;
static int g_process_key_press_calls;
static int g_process_key_release_calls;
static RecordedVkEvent g_vk_events[8];
static size_t g_vk_event_count;

static TypioWlFrontend g_frontend;
static TypioWlKeyboard g_keyboard;
static TypioWlSession g_session;
static int g_manager_storage;
static int g_engine_storage;

static void reset_state(void) {
    memset(&g_frontend, 0, sizeof(g_frontend));
    memset(&g_keyboard, 0, sizeof(g_keyboard));
    memset(&g_session, 0, sizeof(g_session));
    g_manager_storage = 0;
    g_engine_storage = 0;
    memset(g_vk_events, 0, sizeof(g_vk_events));

    g_frontend.instance = (TypioInstance *)&g_frontend;
    g_frontend.active_key_generation = 1;
    g_keyboard.frontend = &g_frontend;
    g_session.frontend = &g_frontend;
    g_session.ctx = (TypioInputContext *)&g_session;

    g_active_engine_name = NULL;
    g_basic_printable_key_mode = "forward";
    g_process_key_result = false;
    g_process_key_calls = 0;
    g_process_key_press_calls = 0;
    g_process_key_release_calls = 0;
    g_vk_event_count = 0;
}

static void set_active_engine(const char *name) {
    g_active_engine_name = name;
}

static void set_basic_printable_key_mode(const char *mode) {
    g_basic_printable_key_mode = mode;
}

TypioEngineManager *typio_instance_get_engine_manager(TypioInstance *instance) {
    (void)instance;
    return (TypioEngineManager *)&g_manager_storage;
}

TypioConfig *typio_instance_get_config(TypioInstance *instance) {
    (void)instance;
    return (TypioConfig *)&g_manager_storage;
}

TypioEngine *typio_engine_manager_get_active(TypioEngineManager *manager) {
    (void)manager;
    return g_active_engine_name ? (TypioEngine *)&g_engine_storage : NULL;
}

const char *typio_engine_get_name(const TypioEngine *engine) {
    (void)engine;
    return g_active_engine_name;
}

const char *typio_config_get_string(const TypioConfig *config,
                                    const char *key,
                                    const char *default_val) {
    (void)config;
    if (key && strcmp(key, "engines.basic.printable_key_mode") == 0) {
        return g_basic_printable_key_mode ? g_basic_printable_key_mode : default_val;
    }
    return default_val;
}

bool typio_input_context_process_key(TypioInputContext *ctx,
                                     const TypioKeyEvent *event) {
    (void)ctx;
    g_process_key_calls++;
    if (event) {
        if (event->type == TYPIO_EVENT_KEY_PRESS) {
            g_process_key_press_calls++;
        } else if (event->type == TYPIO_EVENT_KEY_RELEASE) {
            g_process_key_release_calls++;
        }
    }
    return g_process_key_result;
}

bool typio_wl_candidate_guard_should_consume(TypioInputContext *ctx,
                                             uint32_t keysym) {
    (void)ctx;
    (void)keysym;
    return false;
}

TypioWlStartupSuppressReason typio_wl_startup_guard_classify_press(
    uint64_t created_at_epoch,
    uint64_t current_epoch,
    bool suppress_stale_keys) {
    (void)created_at_epoch;
    (void)current_epoch;
    (void)suppress_stale_keys;
    return TYPIO_WL_STARTUP_SUPPRESS_NONE;
}

bool typio_wl_startup_guard_is_in_guard_window(uint64_t created_at_epoch,
                                               uint64_t current_epoch) {
    (void)created_at_epoch;
    (void)current_epoch;
    return false;
}

bool typio_wl_boundary_bridge_should_forward_orphan_release_cleanup(
    uint32_t keysym,
    uint32_t modifiers,
    bool saw_blocking_modifier) {
    (void)keysym;
    (void)modifiers;
    (void)saw_blocking_modifier;
    return false;
}

void typio_wl_vk_forward_key(TypioWlKeyboard *keyboard,
                             uint32_t time,
                             uint32_t key,
                             uint32_t state,
                             uint32_t unicode) {
    (void)keyboard;
    if (g_vk_event_count < sizeof(g_vk_events) / sizeof(g_vk_events[0])) {
        g_vk_events[g_vk_event_count++] = (RecordedVkEvent){
            .time = time,
            .key = key,
            .state = state,
            .unicode = unicode,
        };
    }
}

uint32_t typio_wl_xkb_effective_modifiers(TypioWlKeyboard *keyboard) {
    (void)keyboard;
    return 0;
}

void typio_wl_key_debug_format(uint32_t unicode, char *buffer, size_t size) {
    snprintf(buffer, size, "unicode=U+%04X", unicode);
}

void typio_wl_key_debug_format_keysym(uint32_t keysym, char *buffer, size_t size) {
    snprintf(buffer, size, "keysym=0x%x", keysym);
}

const char *typio_wl_key_tracking_state_name(TypioKeyTrackState state) {
    switch (state) {
    case TYPIO_KEY_TRACK_FORWARDED:
        return "forwarded";
    case TYPIO_KEY_TRACK_BASIC_PASSTHROUGH:
        return "basic_passthrough";
    case TYPIO_KEY_TRACK_APP_SHORTCUT:
        return "app_shortcut";
    case TYPIO_KEY_TRACK_RELEASED_PENDING:
        return "released_pending";
    case TYPIO_KEY_TRACK_SUPPRESSED_STARTUP:
        return "suppressed_startup";
    case TYPIO_KEY_TRACK_VOICE_PTT:
        return "voice_ptt";
    case TYPIO_KEY_TRACK_VOICE_PTT_UNAVAIL:
        return "voice_ptt_unavail";
    case TYPIO_KEY_TRACK_IDLE:
    default:
        return "idle";
    }
}

void typio_wl_trace(TypioWlFrontend *frontend,
                    const char *category,
                    const char *format, ...) {
    (void)frontend;
    (void)category;
    (void)format;
}

void typio_log(TypioLogLevel level, const char *format, ...) {
    (void)level;
    (void)format;
}

void typio_log_dump_recent_to_configured_path(const char *reason) {
    (void)reason;
}

void typio_wl_keyboard_release_grab(TypioWlKeyboard *keyboard) {
    (void)keyboard;
}

void typio_wl_frontend_stop(TypioWlFrontend *frontend) {
    (void)frontend;
}

void typio_wl_set_preedit(TypioWlFrontend *frontend,
                          const char *text,
                          int cursor_begin,
                          int cursor_end) {
    (void)frontend;
    (void)text;
    (void)cursor_begin;
    (void)cursor_end;
}

void typio_wl_commit(TypioWlFrontend *frontend) {
    (void)frontend;
}

#ifdef HAVE_VOICE
bool typio_voice_service_start(TypioVoiceService *svc) {
    (void)svc;
    return false;
}

void typio_voice_service_stop(TypioVoiceService *svc) {
    (void)svc;
}

bool typio_voice_service_is_available(TypioVoiceService *svc) {
    (void)svc;
    return false;
}

const char *typio_voice_service_get_unavail_reason(TypioVoiceService *svc) {
    (void)svc;
    return "disabled";
}
#endif

TEST(basic_engine_bypasses_for_printable_text) {
    reset_state();
    set_active_engine("basic");
    g_process_key_result = true;

    typio_wl_key_route_process_press(&g_keyboard, &g_session, 30,
                                     'a', TYPIO_MOD_NONE,
                                     'a', 1234);

    ASSERT(g_process_key_calls == 0);
    ASSERT(g_vk_event_count == 1);
    ASSERT(g_vk_events[0].key == 30);
    ASSERT(g_vk_events[0].state == WL_KEYBOARD_KEY_STATE_PRESSED);
    ASSERT(g_frontend.key_states[30] == TYPIO_KEY_TRACK_BASIC_PASSTHROUGH);
}

TEST(basic_engine_forwards_printable_text_release_symmetrically) {
    reset_state();
    set_active_engine("basic");
    g_process_key_result = true;

    typio_wl_key_route_process_press(&g_keyboard, &g_session, 30,
                                     'a', TYPIO_MOD_NONE,
                                     'a', 1234);
    ASSERT(g_frontend.key_states[30] == TYPIO_KEY_TRACK_BASIC_PASSTHROUGH);
    typio_wl_key_route_process_release(&g_keyboard, &g_session, 30,
                                       'a', TYPIO_MOD_NONE,
                                       'a', 1250);

    ASSERT(g_process_key_calls == 0);
    ASSERT(g_process_key_press_calls == 0);
    ASSERT(g_process_key_release_calls == 0);
    ASSERT(g_vk_event_count == 2);
    ASSERT(g_vk_events[0].key == 30);
    ASSERT(g_vk_events[0].state == WL_KEYBOARD_KEY_STATE_PRESSED);
    ASSERT(g_vk_events[1].key == 30);
    ASSERT(g_vk_events[1].state == WL_KEYBOARD_KEY_STATE_RELEASED);
    ASSERT(g_frontend.key_states[30] == TYPIO_KEY_TRACK_IDLE);
}

TEST(forwarded_shift_release_still_reaches_engine) {
    reset_state();
    set_active_engine("rime");
    g_process_key_result = true;

    typio_wl_key_route_process_press(&g_keyboard, &g_session, 42,
                                     XKB_KEY_Shift_L, TYPIO_MOD_NONE,
                                     0, 1234);

    ASSERT(g_process_key_calls == 1);
    ASSERT(g_process_key_press_calls == 1);
    ASSERT(g_process_key_release_calls == 0);
    ASSERT(g_vk_event_count == 1);
    ASSERT(g_vk_events[0].state == WL_KEYBOARD_KEY_STATE_PRESSED);
    ASSERT(g_frontend.key_states[42] == TYPIO_KEY_TRACK_FORWARDED);

    typio_wl_key_route_process_release(&g_keyboard, &g_session, 42,
                                       XKB_KEY_Shift_L, TYPIO_MOD_NONE,
                                       0, 1250);

    ASSERT(g_process_key_calls == 2);
    ASSERT(g_process_key_press_calls == 1);
    ASSERT(g_process_key_release_calls == 1);
    ASSERT(g_vk_event_count == 2);
    ASSERT(g_vk_events[1].state == WL_KEYBOARD_KEY_STATE_RELEASED);
    ASSERT(g_frontend.key_states[42] == TYPIO_KEY_TRACK_IDLE);
}

TEST(non_basic_engine_keeps_engine_in_path) {
    reset_state();
    set_active_engine("rime");
    g_process_key_result = true;

    typio_wl_key_route_process_press(&g_keyboard, &g_session, 30,
                                     'a', TYPIO_MOD_NONE,
                                     'a', 1234);

    ASSERT(g_process_key_calls == 1);
    ASSERT(g_vk_event_count == 0);
    ASSERT(g_frontend.key_states[30] == TYPIO_KEY_TRACK_IDLE);
}

TEST(basic_engine_commit_mode_keeps_engine_in_path) {
    reset_state();
    set_active_engine("basic");
    set_basic_printable_key_mode("commit");
    g_process_key_result = true;

    typio_wl_key_route_process_press(&g_keyboard, &g_session, 30,
                                     'a', TYPIO_MOD_NONE,
                                     'a', 1234);

    ASSERT(g_process_key_calls == 1);
    ASSERT(g_process_key_press_calls == 1);
    ASSERT(g_process_key_release_calls == 0);
    ASSERT(g_vk_event_count == 0);
    ASSERT(g_frontend.key_states[30] == TYPIO_KEY_TRACK_IDLE);
}

int main(void) {
    printf("Running key route tests:\n");
    run_test_basic_engine_bypasses_for_printable_text();
    run_test_basic_engine_forwards_printable_text_release_symmetrically();
    run_test_forwarded_shift_release_still_reaches_engine();
    run_test_non_basic_engine_keeps_engine_in_path();
    run_test_basic_engine_commit_mode_keeps_engine_in_path();
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
