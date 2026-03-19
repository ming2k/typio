/**
 * @file test_frontend_aux.c
 * @brief Tests for auxiliary frontend FD degradation policy
 */

#include "frontend_aux.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>

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

TEST(ignores_normal_readable_events) {
    ASSERT(!typio_wl_aux_should_disable_on_revents(POLLIN));
    ASSERT(!typio_wl_aux_should_disable_on_revents(POLLIN | POLLOUT));
    ASSERT(!typio_wl_aux_should_disable_on_revents(0));
}

TEST(disables_on_terminal_poll_errors) {
    ASSERT(typio_wl_aux_should_disable_on_revents(POLLERR));
    ASSERT(typio_wl_aux_should_disable_on_revents(POLLHUP));
    ASSERT(typio_wl_aux_should_disable_on_revents(POLLNVAL));
    ASSERT(typio_wl_aux_should_disable_on_revents(POLLIN | POLLERR));
}

TEST(disables_only_on_negative_dispatch_result) {
    ASSERT(!typio_wl_aux_should_disable_on_dispatch_result(0));
    ASSERT(!typio_wl_aux_should_disable_on_dispatch_result(1));
    ASSERT(typio_wl_aux_should_disable_on_dispatch_result(-1));
}

TEST(transition_disables_source_on_poll_error) {
    TypioWlAuxState state = { .fd = 42, .disabled = false };

    state = typio_wl_aux_apply_transition(state, POLLERR, 0, 77);

    ASSERT(state.disabled);
    ASSERT(state.fd == -1);
}

TEST(transition_disables_source_on_dispatch_failure) {
    TypioWlAuxState state = { .fd = 42, .disabled = false };

    state = typio_wl_aux_apply_transition(state, POLLIN, -1, 77);

    ASSERT(state.disabled);
    ASSERT(state.fd == -1);
}

TEST(transition_refreshes_fd_after_successful_dispatch) {
    TypioWlAuxState state = { .fd = 42, .disabled = false };

    state = typio_wl_aux_apply_transition(state, POLLIN, 0, 77);

    ASSERT(!state.disabled);
    ASSERT(state.fd == 77);
}

TEST(transition_leaves_state_unchanged_when_idle) {
    TypioWlAuxState state = { .fd = 42, .disabled = false };

    state = typio_wl_aux_apply_transition(state, 0, 0, 77);

    ASSERT(!state.disabled);
    ASSERT(state.fd == 42);
}

int main(void) {
    printf("Running frontend aux tests:\n");

    run_test_ignores_normal_readable_events();
    run_test_disables_on_terminal_poll_errors();
    run_test_disables_only_on_negative_dispatch_result();
    run_test_transition_disables_source_on_poll_error();
    run_test_transition_disables_source_on_dispatch_failure();
    run_test_transition_refreshes_fd_after_successful_dispatch();
    run_test_transition_leaves_state_unchanged_when_idle();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
