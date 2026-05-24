/**
 * @file test_checkpoint_codec.c
 * @brief Session-checkpoint wire format: encode/decode round-trip + validation
 */

#include "checkpoint_codec.h"

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
    } while(0)

static TypioCkpRecord base_record(void) {
    TypioCkpRecord r;
    r.version = TYPIO_CKP_VERSION;
    r.engine_name = "rime";
    r.engine_name_len = 0;
    r.identity = "app:firefox";
    r.identity_len = 0;
    r.boottime_ms = 100000;
    r.blob = "0nihao";
    r.blob_size = 6;
    return r;
}

TEST(round_trip_preserves_fields) {
    TypioCkpRecord in = base_record();
    size_t size = 0;
    char *buf = typio_ckp_encode(&in, &size);
    ASSERT(buf != NULL);
    ASSERT(size > 0);

    TypioCkpRecord out;
    ASSERT(typio_ckp_decode(buf, size, &out));
    ASSERT(out.version == TYPIO_CKP_VERSION);
    ASSERT(out.engine_name_len == 4);
    ASSERT(memcmp(out.engine_name, "rime", 4) == 0);
    ASSERT(out.identity_len == strlen("app:firefox"));
    ASSERT(memcmp(out.identity, "app:firefox", out.identity_len) == 0);
    ASSERT(out.boottime_ms == 100000);
    ASSERT(out.blob_size == 6);
    ASSERT(memcmp(out.blob, "0nihao", 6) == 0);

    free(buf);
}

TEST(empty_blob_and_identity_round_trip) {
    TypioCkpRecord in = base_record();
    in.identity = "";
    in.blob = NULL;
    in.blob_size = 0;
    size_t size = 0;
    char *buf = typio_ckp_encode(&in, &size);
    ASSERT(buf != NULL);

    TypioCkpRecord out;
    ASSERT(typio_ckp_decode(buf, size, &out));
    ASSERT(out.identity_len == 0);
    ASSERT(out.blob_size == 0);
    ASSERT(out.blob == NULL);
    free(buf);
}

TEST(blob_with_embedded_nuls_and_high_bytes) {
    /* Binary-safe: blob may contain NULs and bytes > 0x7F. */
    const char blob[] = { 0x00, (char)0xE4, (char)0xBD, 0x00, (char)0xA0 };
    TypioCkpRecord in = base_record();
    in.blob = blob;
    in.blob_size = sizeof(blob);
    size_t size = 0;
    char *buf = typio_ckp_encode(&in, &size);
    ASSERT(buf != NULL);

    TypioCkpRecord out;
    ASSERT(typio_ckp_decode(buf, size, &out));
    ASSERT(out.blob_size == sizeof(blob));
    ASSERT(memcmp(out.blob, blob, sizeof(blob)) == 0);
    free(buf);
}

TEST(decode_rejects_bad_magic) {
    TypioCkpRecord in = base_record();
    size_t size = 0;
    char *buf = typio_ckp_encode(&in, &size);
    ASSERT(buf != NULL);
    buf[0] = 'X'; /* corrupt magic */
    TypioCkpRecord out;
    ASSERT(!typio_ckp_decode(buf, size, &out));
    free(buf);
}

TEST(decode_rejects_truncation) {
    TypioCkpRecord in = base_record();
    size_t size = 0;
    char *buf = typio_ckp_encode(&in, &size);
    ASSERT(buf != NULL);
    /* Every truncated prefix must be rejected, never over-read. */
    for (size_t cut = 0; cut < size; ++cut) {
        TypioCkpRecord out;
        ASSERT(!typio_ckp_decode(buf, cut, &out));
    }
    /* The full buffer still decodes. */
    TypioCkpRecord ok;
    ASSERT(typio_ckp_decode(buf, size, &ok));
    free(buf);
}

TEST(decode_rejects_oversized_length_prefix) {
    /* A length field claiming more bytes than remain must be rejected. */
    TypioCkpRecord in = base_record();
    size_t size = 0;
    char *buf = typio_ckp_encode(&in, &size);
    ASSERT(buf != NULL);
    /* engine_name length lives at offset 12 (after 8 magic + 4 version);
     * inflate it well past the buffer. */
    buf[12] = (char)0xFF;
    buf[13] = (char)0xFF;
    TypioCkpRecord out;
    ASSERT(!typio_ckp_decode(buf, size, &out));
    free(buf);
}

TEST(encode_rejects_null_engine_name) {
    TypioCkpRecord in = base_record();
    in.engine_name = NULL;
    size_t size = 0;
    ASSERT(typio_ckp_encode(&in, &size) == NULL);
}

TEST(valid_for_matching_engine_and_fresh_stamp) {
    TypioCkpRecord in = base_record();
    in.boottime_ms = 100000;
    size_t size = 0;
    char *buf = typio_ckp_encode(&in, &size);
    TypioCkpRecord out;
    ASSERT(typio_ckp_decode(buf, size, &out));
    /* now = 130000, age 30s, window 60s -> fresh */
    ASSERT(typio_ckp_is_valid(&out, "rime", 130000, 60000));
    free(buf);
}

TEST(invalid_for_engine_mismatch) {
    TypioCkpRecord in = base_record();
    size_t size = 0;
    char *buf = typio_ckp_encode(&in, &size);
    TypioCkpRecord out;
    ASSERT(typio_ckp_decode(buf, size, &out));
    ASSERT(!typio_ckp_is_valid(&out, "mozc", 130000, 60000));
    /* prefix match must not pass either */
    ASSERT(!typio_ckp_is_valid(&out, "rim", 130000, 60000));
    ASSERT(!typio_ckp_is_valid(&out, "rimex", 130000, 60000));
    free(buf);
}

TEST(invalid_for_stale_stamp) {
    TypioCkpRecord in = base_record();
    in.boottime_ms = 100000;
    size_t size = 0;
    char *buf = typio_ckp_encode(&in, &size);
    TypioCkpRecord out;
    ASSERT(typio_ckp_decode(buf, size, &out));
    /* now = 100000 + 120s, window 60s -> too old */
    ASSERT(!typio_ckp_is_valid(&out, "rime", 100000 + 120000, 60000));
    free(buf);
}

TEST(invalid_for_future_stamp_previous_boot) {
    /* After a reboot, current boottime is small; a checkpoint stamped at a
     * larger boottime is from a previous boot and must be rejected. */
    TypioCkpRecord in = base_record();
    in.boottime_ms = 500000;
    size_t size = 0;
    char *buf = typio_ckp_encode(&in, &size);
    TypioCkpRecord out;
    ASSERT(typio_ckp_decode(buf, size, &out));
    ASSERT(!typio_ckp_is_valid(&out, "rime", 10000, 60000));
    free(buf);
}

TEST(invalid_for_version_mismatch) {
    TypioCkpRecord in = base_record();
    in.version = TYPIO_CKP_VERSION + 1;
    size_t size = 0;
    char *buf = typio_ckp_encode(&in, &size);
    TypioCkpRecord out;
    ASSERT(typio_ckp_decode(buf, size, &out));
    ASSERT(out.version == TYPIO_CKP_VERSION + 1);
    ASSERT(!typio_ckp_is_valid(&out, "rime", 130000, 60000));
    free(buf);
}

int main(void) {
    printf("Running checkpoint codec tests:\n");

    run_test_round_trip_preserves_fields();
    run_test_empty_blob_and_identity_round_trip();
    run_test_blob_with_embedded_nuls_and_high_bytes();
    run_test_decode_rejects_bad_magic();
    run_test_decode_rejects_truncation();
    run_test_decode_rejects_oversized_length_prefix();
    run_test_encode_rejects_null_engine_name();
    run_test_valid_for_matching_engine_and_fresh_stamp();
    run_test_invalid_for_engine_mismatch();
    run_test_invalid_for_stale_stamp();
    run_test_invalid_for_future_stamp_previous_boot();
    run_test_invalid_for_version_mismatch();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
