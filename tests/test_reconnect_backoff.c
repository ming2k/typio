/**
 * @file test_reconnect_backoff.c
 * @brief Capped-exponential reconnect backoff schedule tests
 */

#include "reconnect_backoff.h"

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

TEST(first_delay_is_base) {
    ASSERT(typio_wl_reconnect_delay_ms(0) == TYPIO_WL_RECONNECT_BASE_DELAY_MS);
}

TEST(delay_doubles_until_cap) {
    ASSERT(typio_wl_reconnect_delay_ms(1) == 500);
    ASSERT(typio_wl_reconnect_delay_ms(2) == 1000);
    ASSERT(typio_wl_reconnect_delay_ms(3) == 2000);
    ASSERT(typio_wl_reconnect_delay_ms(4) == 4000);
    ASSERT(typio_wl_reconnect_delay_ms(5) == 8000);
}

TEST(delay_is_clamped_at_cap) {
    ASSERT(typio_wl_reconnect_delay_ms(6) == TYPIO_WL_RECONNECT_MAX_DELAY_MS);
    ASSERT(typio_wl_reconnect_delay_ms(10) == TYPIO_WL_RECONNECT_MAX_DELAY_MS);
}

TEST(delay_never_overflows_for_large_attempt) {
    /* Must not wrap to a tiny value for absurd attempt counts. */
    ASSERT(typio_wl_reconnect_delay_ms(31) == TYPIO_WL_RECONNECT_MAX_DELAY_MS);
    ASSERT(typio_wl_reconnect_delay_ms(64) == TYPIO_WL_RECONNECT_MAX_DELAY_MS);
    ASSERT(typio_wl_reconnect_delay_ms(0xFFFFFFFFu) == TYPIO_WL_RECONNECT_MAX_DELAY_MS);
}

TEST(delay_is_monotonic_nondecreasing) {
    uint32_t prev = 0;
    for (uint32_t a = 0; a < 20; ++a) {
        uint32_t d = typio_wl_reconnect_delay_ms(a);
        ASSERT(d >= prev);
        ASSERT(d <= TYPIO_WL_RECONNECT_MAX_DELAY_MS);
        prev = d;
    }
}

TEST(retry_stops_at_cap) {
    ASSERT(typio_wl_reconnect_should_retry(0));
    ASSERT(typio_wl_reconnect_should_retry(TYPIO_WL_RECONNECT_MAX_ATTEMPTS - 1));
    ASSERT(!typio_wl_reconnect_should_retry(TYPIO_WL_RECONNECT_MAX_ATTEMPTS));
    ASSERT(!typio_wl_reconnect_should_retry(TYPIO_WL_RECONNECT_MAX_ATTEMPTS + 5));
}

int main(void) {
    printf("Running reconnect backoff tests:\n");

    run_test_first_delay_is_base();
    run_test_delay_doubles_until_cap();
    run_test_delay_is_clamped_at_cap();
    run_test_delay_never_overflows_for_large_attempt();
    run_test_delay_is_monotonic_nondecreasing();
    run_test_retry_stops_at_cap();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
