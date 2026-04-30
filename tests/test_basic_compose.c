/**
 * @file test_basic_compose.c
 * @brief Unit tests for the basic engine compose/dead-key state machine
 */

#include "compose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NULL(a) ASSERT((a) == NULL)
#define ASSERT_NOT_NULL(a) ASSERT((a) != NULL)

TEST(idle_passes_through) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    /* 'q' is not a compose prefix */
    BasicComposeResult r = basic_compose_process_key(c, 'q', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_NONE);
    ASSERT_EQ(count, 0);

    basic_compose_free(c);
}

TEST(acute_accent_a) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    BasicComposeResult r = basic_compose_process_key(c, '\'', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_CONSUME);
    ASSERT_EQ(count, 0);
    ASSERT(basic_compose_is_active(c));

    r = basic_compose_process_key(c, 'a', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(out[0], 0x00E1); /* á */
    ASSERT(!basic_compose_is_active(c));

    basic_compose_free(c);
}

TEST(acute_accent_uppercase_e) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '\'', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, 'E', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(out[0], 0x00C9); /* É */

    basic_compose_free(c);
}

TEST(grave_accent_e) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '`', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, 'e', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(out[0], 0x00E8); /* è */

    basic_compose_free(c);
}

TEST(circumflex_u) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '^', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, 'u', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(out[0], 0x00FB); /* û */

    basic_compose_free(c);
}

TEST(diaeresis_o) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '"', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, 'o', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(out[0], 0x00F6); /* ö */

    basic_compose_free(c);
}

TEST(tilde_n) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '~', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, 'n', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(out[0], 0x00F1); /* ñ */

    basic_compose_free(c);
}

TEST(cedilla_c) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, ',', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, 'c', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(out[0], 0x00E7); /* ç */

    basic_compose_free(c);
}

TEST(no_match_flushes_first_key) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '\'', out, &count), BASIC_COMPOSE_CONSUME);
    /* ' + x is not a rule */
    BasicComposeResult r = basic_compose_process_key(c, 'x', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_CANCEL);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(out[0], '\'');
    ASSERT(!basic_compose_is_active(c));

    basic_compose_free(c);
}

TEST(cancel_returns_first_key) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '\'', out, &count), BASIC_COMPOSE_CONSUME);
    uint32_t cp = basic_compose_cancel(c);
    ASSERT_EQ(cp, '\'');
    ASSERT(!basic_compose_is_active(c));

    basic_compose_free(c);
}

TEST(reset_clears_state) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '~', out, &count), BASIC_COMPOSE_CONSUME);
    ASSERT(basic_compose_is_active(c));
    basic_compose_reset(c);
    ASSERT(!basic_compose_is_active(c));

    basic_compose_free(c);
}

TEST(preedit_returns_first_key) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_NULL(basic_compose_get_preedit(c));
    ASSERT_EQ(basic_compose_process_key(c, '`', out, &count), BASIC_COMPOSE_CONSUME);
    const char *pre = basic_compose_get_preedit(c);
    ASSERT_NOT_NULL(pre);
    ASSERT_EQ(pre[0], '`');
    ASSERT_EQ(pre[1], '\0');

    basic_compose_free(c);
}

TEST(en_dash) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '-', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, '-', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(out[0], 0x2013); /* – */

    basic_compose_free(c);
}

TEST(em_dash) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '-', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, '=', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(out[0], 0x2014); /* — */

    basic_compose_free(c);
}

TEST(ellipsis) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '.', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, '.', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(out[0], 0x2026); /* … */

    basic_compose_free(c);
}

TEST(inverted_question) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '?', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, '?', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(out[0], 0x00BF); /* ¿ */

    basic_compose_free(c);
}

TEST(inverted_exclamation) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '!', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, '!', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(out[0], 0x00A1); /* ¡ */

    basic_compose_free(c);
}

TEST(guillemets) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '<', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, '<', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(out[0], 0x00AB); /* « */

    basic_compose_reset(c);

    ASSERT_EQ(basic_compose_process_key(c, '>', out, &count), BASIC_COMPOSE_CONSUME);
    r = basic_compose_process_key(c, '>', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(out[0], 0x00BB); /* » */

    basic_compose_free(c);
}

TEST(slash_o) {
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '/', out, &count), BASIC_COMPOSE_CONSUME);
    BasicComposeResult r = basic_compose_process_key(c, 'o', out, &count);
    ASSERT_EQ(r, BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(out[0], 0x00F8); /* ø */

    basic_compose_free(c);
}

TEST(multiple_sequences_stateless) {
    /* Ensure state is properly reset after each sequence. */
    BasicCompose *c = basic_compose_new();
    uint32_t out[4];
    size_t count = 0;

    ASSERT_EQ(basic_compose_process_key(c, '\'', out, &count), BASIC_COMPOSE_CONSUME);
    ASSERT_EQ(basic_compose_process_key(c, 'e', out, &count), BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(out[0], 0x00E9);

    ASSERT_EQ(basic_compose_process_key(c, '\'', out, &count), BASIC_COMPOSE_CONSUME);
    ASSERT_EQ(basic_compose_process_key(c, 'a', out, &count), BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(out[0], 0x00E1);

    ASSERT_EQ(basic_compose_process_key(c, '~', out, &count), BASIC_COMPOSE_CONSUME);
    ASSERT_EQ(basic_compose_process_key(c, 'n', out, &count), BASIC_COMPOSE_COMMIT);
    ASSERT_EQ(out[0], 0x00F1);

    basic_compose_free(c);
}

int main(void) {
    printf("Running basic_compose tests...\n");

    run_test_idle_passes_through();
    run_test_acute_accent_a();
    run_test_acute_accent_uppercase_e();
    run_test_grave_accent_e();
    run_test_circumflex_u();
    run_test_diaeresis_o();
    run_test_tilde_n();
    run_test_cedilla_c();
    run_test_no_match_flushes_first_key();
    run_test_cancel_returns_first_key();
    run_test_reset_clears_state();
    run_test_preedit_returns_first_key();
    run_test_en_dash();
    run_test_em_dash();
    run_test_ellipsis();
    run_test_inverted_question();
    run_test_inverted_exclamation();
    run_test_guillemets();
    run_test_slash_o();
    run_test_multiple_sequences_stateless();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
