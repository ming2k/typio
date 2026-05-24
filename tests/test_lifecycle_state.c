/**
 * @file test_lifecycle_state.c
 * @brief Orthogonal lifecycle-axis projection and agreement tests
 */

#include "lifecycle_state.h"

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
    } while(0)

static TypioWlLifecycleState mk(TypioWlConnState c, TypioWlFocusState f,
                                TypioWlGrabState g, TypioWlCompState m) {
    TypioWlLifecycleState s = { .conn = c, .focus = f, .grab = g, .comp = m };
    return s;
}

TEST(disconnected_projects_inactive) {
    TypioWlLifecycleState s = mk(TYPIO_WL_CONN_DISCONNECTED,
                                 TYPIO_WL_FOCUS_FOCUSED,
                                 TYPIO_WL_GRAB_READY,
                                 TYPIO_WL_COMP_COMPOSING);
    ASSERT(typio_wl_lifecycle_project_phase(&s) == TYPIO_WL_PHASE_INACTIVE);
}

TEST(unfocused_projects_inactive) {
    TypioWlLifecycleState s = mk(TYPIO_WL_CONN_CONNECTED,
                                 TYPIO_WL_FOCUS_UNFOCUSED,
                                 TYPIO_WL_GRAB_READY,
                                 TYPIO_WL_COMP_IDLE);
    ASSERT(typio_wl_lifecycle_project_phase(&s) == TYPIO_WL_PHASE_INACTIVE);
}

TEST(focused_with_ready_grab_projects_active) {
    TypioWlLifecycleState s = mk(TYPIO_WL_CONN_CONNECTED,
                                 TYPIO_WL_FOCUS_FOCUSED,
                                 TYPIO_WL_GRAB_READY,
                                 TYPIO_WL_COMP_IDLE);
    ASSERT(typio_wl_lifecycle_project_phase(&s) == TYPIO_WL_PHASE_ACTIVE);
}

TEST(focused_without_grab_projects_activating) {
    TypioWlLifecycleState s = mk(TYPIO_WL_CONN_CONNECTED,
                                 TYPIO_WL_FOCUS_FOCUSED,
                                 TYPIO_WL_GRAB_NONE,
                                 TYPIO_WL_COMP_IDLE);
    ASSERT(typio_wl_lifecycle_project_phase(&s) == TYPIO_WL_PHASE_ACTIVATING);
}

TEST(focused_pending_keymap_projects_activating) {
    TypioWlLifecycleState s = mk(TYPIO_WL_CONN_CONNECTED,
                                 TYPIO_WL_FOCUS_FOCUSED,
                                 TYPIO_WL_GRAB_PENDING_KEYMAP,
                                 TYPIO_WL_COMP_IDLE);
    ASSERT(typio_wl_lifecycle_project_phase(&s) == TYPIO_WL_PHASE_ACTIVATING);
}

TEST(null_state_projects_inactive) {
    ASSERT(typio_wl_lifecycle_project_phase(NULL) == TYPIO_WL_PHASE_INACTIVE);
}

TEST(agreement_holds_for_matching_active) {
    TypioWlLifecycleState s = mk(TYPIO_WL_CONN_CONNECTED,
                                 TYPIO_WL_FOCUS_FOCUSED,
                                 TYPIO_WL_GRAB_READY,
                                 TYPIO_WL_COMP_IDLE);
    ASSERT(typio_wl_lifecycle_state_agrees(&s, TYPIO_WL_PHASE_ACTIVE));
}

TEST(divergence_detected_when_grab_lost_but_phase_active) {
    /* The post-resume / post-compositor-restart failure: we believe we are
     * ACTIVE, but reality has no grab. This must be flagged. */
    TypioWlLifecycleState s = mk(TYPIO_WL_CONN_CONNECTED,
                                 TYPIO_WL_FOCUS_FOCUSED,
                                 TYPIO_WL_GRAB_NONE,
                                 TYPIO_WL_COMP_COMPOSING);
    ASSERT(!typio_wl_lifecycle_state_agrees(&s, TYPIO_WL_PHASE_ACTIVE));
}

TEST(divergence_detected_when_unfocused_but_phase_active) {
    TypioWlLifecycleState s = mk(TYPIO_WL_CONN_CONNECTED,
                                 TYPIO_WL_FOCUS_UNFOCUSED,
                                 TYPIO_WL_GRAB_NONE,
                                 TYPIO_WL_COMP_IDLE);
    ASSERT(!typio_wl_lifecycle_state_agrees(&s, TYPIO_WL_PHASE_ACTIVE));
}

TEST(transient_phases_never_diverge) {
    /* DEACTIVATING / ACTIVATING are mid-handshake; the steady-state
     * projection cannot represent them and must not be flagged. */
    TypioWlLifecycleState s = mk(TYPIO_WL_CONN_CONNECTED,
                                 TYPIO_WL_FOCUS_FOCUSED,
                                 TYPIO_WL_GRAB_NONE,
                                 TYPIO_WL_COMP_IDLE);
    ASSERT(typio_wl_lifecycle_state_agrees(&s, TYPIO_WL_PHASE_DEACTIVATING));
    ASSERT(typio_wl_lifecycle_state_agrees(&s, TYPIO_WL_PHASE_ACTIVATING));
}

TEST(inactive_agreement_holds_when_truly_inactive) {
    TypioWlLifecycleState s = mk(TYPIO_WL_CONN_CONNECTED,
                                 TYPIO_WL_FOCUS_UNFOCUSED,
                                 TYPIO_WL_GRAB_NONE,
                                 TYPIO_WL_COMP_IDLE);
    ASSERT(typio_wl_lifecycle_state_agrees(&s, TYPIO_WL_PHASE_INACTIVE));
}

TEST(state_names_are_nonempty) {
    ASSERT(typio_wl_conn_state_name(TYPIO_WL_CONN_CONNECTED)[0] != '\0');
    ASSERT(typio_wl_focus_state_name(TYPIO_WL_FOCUS_FOCUSED)[0] != '\0');
    ASSERT(typio_wl_grab_state_name(TYPIO_WL_GRAB_READY)[0] != '\0');
    ASSERT(typio_wl_comp_state_name(TYPIO_WL_COMP_COMPOSING)[0] != '\0');
}

int main(void) {
    printf("Running lifecycle state tests:\n");

    run_test_disconnected_projects_inactive();
    run_test_unfocused_projects_inactive();
    run_test_focused_with_ready_grab_projects_active();
    run_test_focused_without_grab_projects_activating();
    run_test_focused_pending_keymap_projects_activating();
    run_test_null_state_projects_inactive();
    run_test_agreement_holds_for_matching_active();
    run_test_divergence_detected_when_grab_lost_but_phase_active();
    run_test_divergence_detected_when_unfocused_but_phase_active();
    run_test_transient_phases_never_diverge();
    run_test_inactive_agreement_holds_when_truly_inactive();
    run_test_state_names_are_nonempty();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
