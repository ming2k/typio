/**
 * @file test_vk_bridge.c
 * @brief Tests for virtual keyboard state transitions and fail-safe behavior
 */

#include "wl_frontend_internal.h"
#include "vk_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int stop_count = 0;
static int release_count = 0;

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

void typio_wl_frontend_stop([[maybe_unused]] TypioWlFrontend *frontend) {
    stop_count++;
}

void typio_wl_keyboard_release_grab([[maybe_unused]] TypioWlKeyboard *keyboard) {
    release_count++;
}

void typio_wl_frontend_emit_runtime_state_changed([[maybe_unused]] TypioWlFrontend *frontend) {
}

void typio_wl_trace([[maybe_unused]] TypioWlFrontend *frontend,
                    [[maybe_unused]] const char *topic,
                    [[maybe_unused]] const char *format, ...) {
}

void typio_wl_trace_level([[maybe_unused]] TypioLogLevel level,
                          [[maybe_unused]] TypioWlFrontend *frontend,
                          [[maybe_unused]] const char *topic,
                          [[maybe_unused]] const char *format, ...) {
}

void typio_wl_key_debug_format([[maybe_unused]] uint32_t unicode,
                               char *buffer,
                               size_t size) {
    if (!buffer || size == 0) {
        return;
    }
    snprintf(buffer, size, "unicode");
}

size_t typio_wl_key_tracking_mark_released_pending([[maybe_unused]] TypioKeyTrackState *states,
                                                   [[maybe_unused]] size_t count) {
    return 0;
}

const char *typio_wl_key_tracking_state_name([[maybe_unused]] TypioKeyTrackState state) {
    return "idle";
}

static void reset_counts(void) {
    stop_count = 0;
    release_count = 0;
}

static void init_frontend(TypioWlFrontend *frontend, TypioWlKeyboard *keyboard) {
    memset(frontend, 0, sizeof(*frontend));
    memset(keyboard, 0, sizeof(*keyboard));
    keyboard->frontend = frontend;
    frontend->keyboard = keyboard;
    frontend->virtual_keyboard = (struct zwp_virtual_keyboard_v1 *)0x1;
    frontend->running = true;
    frontend->virtual_keyboard_state = TYPIO_WL_VK_STATE_ABSENT;
}

TEST(expect_keymap_sets_needs_keymap_state_and_deadline) {
    TypioWlFrontend frontend;
    TypioWlKeyboard keyboard;

    init_frontend(&frontend, &keyboard);

    typio_wl_vk_expect_keymap(&frontend, "test");

    ASSERT(frontend.virtual_keyboard_state == TYPIO_WL_VK_STATE_NEEDS_KEYMAP);
    ASSERT(!frontend.virtual_keyboard_has_keymap);
    ASSERT(frontend.virtual_keyboard_keymap_deadline_ms > 0);
}

TEST(ready_state_clears_deadline_and_marks_keymap_present) {
    TypioWlFrontend frontend;
    TypioWlKeyboard keyboard;

    init_frontend(&frontend, &keyboard);
    frontend.virtual_keyboard_keymap_deadline_ms = 1234;

    typio_wl_vk_set_state(&frontend, TYPIO_WL_VK_STATE_READY, "test");

    ASSERT(frontend.virtual_keyboard_state == TYPIO_WL_VK_STATE_READY);
    ASSERT(frontend.virtual_keyboard_has_keymap);
    ASSERT(frontend.virtual_keyboard_keymap_deadline_ms == 0);
}

TEST(broken_state_triggers_fail_safe) {
    TypioWlFrontend frontend;
    TypioWlKeyboard keyboard;

    init_frontend(&frontend, &keyboard);
    frontend.virtual_keyboard_state = TYPIO_WL_VK_STATE_BROKEN;
    reset_counts();

    ASSERT(!typio_wl_vk_is_ready(&frontend, "key"));
    ASSERT(stop_count == 1);
    ASSERT(release_count == 1);
}

TEST(keymap_timeout_triggers_fail_safe) {
    TypioWlFrontend frontend;
    TypioWlKeyboard keyboard;

    init_frontend(&frontend, &keyboard);
    keyboard.grab = (struct zwp_input_method_keyboard_grab_v2 *)0x1;
    frontend.virtual_keyboard_state = TYPIO_WL_VK_STATE_NEEDS_KEYMAP;
    frontend.virtual_keyboard_keymap_deadline_ms = 1;
    frontend.virtual_keyboard_state_since_ms = 0;
    reset_counts();

    typio_wl_vk_health_check(&frontend);

    ASSERT(stop_count == 1);
    ASSERT(release_count == 1);
}

TEST(cancelling_keymap_wait_restores_ready_state_after_grab_teardown) {
    TypioWlFrontend frontend;
    TypioWlKeyboard keyboard;

    init_frontend(&frontend, &keyboard);
    frontend.virtual_keyboard_state = TYPIO_WL_VK_STATE_NEEDS_KEYMAP;
    frontend.virtual_keyboard_keymap_deadline_ms = 1234;
    frontend.virtual_keyboard_last_keymap_ms = 42;

    typio_wl_vk_cancel_keymap_wait(&frontend, "test");

    ASSERT(frontend.virtual_keyboard_state == TYPIO_WL_VK_STATE_READY);
    ASSERT(frontend.virtual_keyboard_has_keymap);
    ASSERT(frontend.virtual_keyboard_keymap_deadline_ms == 0);
}

TEST(keymap_timeout_without_active_grab_is_ignored) {
    TypioWlFrontend frontend;
    TypioWlKeyboard keyboard;

    init_frontend(&frontend, &keyboard);
    frontend.virtual_keyboard_state = TYPIO_WL_VK_STATE_NEEDS_KEYMAP;
    frontend.virtual_keyboard_keymap_deadline_ms = 1;
    frontend.keyboard = NULL;
    reset_counts();

    typio_wl_vk_health_check(&frontend);

    ASSERT(stop_count == 0);
    ASSERT(release_count == 0);
}

int main(void) {
    printf("Running virtual keyboard bridge tests:\n");

    run_test_expect_keymap_sets_needs_keymap_state_and_deadline();
    run_test_ready_state_clears_deadline_and_marks_keymap_present();
    run_test_broken_state_triggers_fail_safe();
    run_test_keymap_timeout_triggers_fail_safe();
    run_test_cancelling_keymap_wait_restores_ready_state_after_grab_teardown();
    run_test_keymap_timeout_without_active_grab_is_ignored();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
