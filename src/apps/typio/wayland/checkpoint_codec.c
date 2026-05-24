/**
 * @file checkpoint_codec.c
 * @brief Pure session-checkpoint wire format (see checkpoint_codec.h)
 *
 * Layout (all integers little-endian, explicit byte order so a checkpoint
 * is not architecture-sensitive):
 *
 *   offset  size  field
 *   0       8     magic "TYPIOCK1"
 *   8       4     version
 *   12      4     engine_name length (N1)
 *   16      N1    engine_name bytes (no NUL)
 *   ..      4     identity length (N2)
 *   ..      N2    identity bytes (no NUL)
 *   ..      8     boottime_ms
 *   ..      4     blob length (N3)
 *   ..      N3    blob bytes
 */

#include "checkpoint_codec.h"

#include <stdlib.h>
#include <string.h>

static void put_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void put_u64(unsigned char *p, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        p[i] = (unsigned char)((v >> (8 * i)) & 0xFF);
}

static uint32_t get_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

char *typio_ckp_encode(const TypioCkpRecord *rec, size_t *out_size) {
    size_t name_len;
    size_t id_len;
    size_t total;
    unsigned char *buf;
    unsigned char *p;

    if (!rec || !rec->engine_name)
        return nullptr;

    name_len = strlen(rec->engine_name);
    id_len = rec->identity ? strlen(rec->identity) : 0;

    /* Bound the variable-length fields so the u32 length prefixes cannot
     * overflow and a malformed caller cannot request an absurd buffer. */
    if (name_len > 0xFFFF || id_len > 0xFFFF || rec->blob_size > (size_t)0x7FFFFFFF)
        return nullptr;

    total = TYPIO_CKP_MAGIC_LEN + 4 /*ver*/
            + 4 + name_len
            + 4 + id_len
            + 8 /*boottime*/
            + 4 + rec->blob_size;

    buf = malloc(total);
    if (!buf)
        return nullptr;

    p = buf;
    memcpy(p, TYPIO_CKP_MAGIC, TYPIO_CKP_MAGIC_LEN); p += TYPIO_CKP_MAGIC_LEN;
    put_u32(p, rec->version); p += 4;
    put_u32(p, (uint32_t)name_len); p += 4;
    memcpy(p, rec->engine_name, name_len); p += name_len;
    put_u32(p, (uint32_t)id_len); p += 4;
    if (id_len) { memcpy(p, rec->identity, id_len); p += id_len; }
    put_u64(p, rec->boottime_ms); p += 8;
    put_u32(p, (uint32_t)rec->blob_size); p += 4;
    if (rec->blob_size) { memcpy(p, rec->blob, rec->blob_size); p += rec->blob_size; }

    if (out_size)
        *out_size = total;
    return (char *)buf;
}

bool typio_ckp_decode(const char *buf, size_t size, TypioCkpRecord *out) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t remaining = size;
    uint32_t name_len;
    uint32_t id_len;
    uint32_t blob_len;
    const unsigned char *name_ptr;
    const unsigned char *id_ptr;

    if (!buf || !out)
        return false;

    /* magic + version */
    if (remaining < TYPIO_CKP_MAGIC_LEN + 4)
        return false;
    if (memcmp(p, TYPIO_CKP_MAGIC, TYPIO_CKP_MAGIC_LEN) != 0)
        return false;
    p += TYPIO_CKP_MAGIC_LEN; remaining -= TYPIO_CKP_MAGIC_LEN;
    out->version = get_u32(p); p += 4; remaining -= 4;

    /* engine_name */
    if (remaining < 4) return false;
    name_len = get_u32(p); p += 4; remaining -= 4;
    if (name_len > remaining) return false;
    name_ptr = p;
    p += name_len; remaining -= name_len;

    /* identity */
    if (remaining < 4) return false;
    id_len = get_u32(p); p += 4; remaining -= 4;
    if (id_len > remaining) return false;
    id_ptr = p;
    p += id_len; remaining -= id_len;

    /* boottime */
    if (remaining < 8) return false;
    out->boottime_ms = get_u64(p); p += 8; remaining -= 8;

    /* blob */
    if (remaining < 4) return false;
    blob_len = get_u32(p); p += 4; remaining -= 4;
    if (blob_len > remaining) return false;

    out->engine_name = (const char *)name_ptr;
    out->engine_name_len = name_len;
    out->identity = (const char *)id_ptr;
    out->identity_len = id_len;
    out->blob = blob_len ? (const char *)p : nullptr;
    out->blob_size = blob_len;
    return true;
}

bool typio_ckp_is_valid(const TypioCkpRecord *rec,
                        const char *expected_engine,
                        uint64_t now_boottime_ms,
                        uint64_t max_age_ms) {
    size_t expect_len;

    if (!rec || !expected_engine)
        return false;

    if (rec->version != TYPIO_CKP_VERSION)
        return false;

    /* engine_name is length-bounded (not NUL-terminated) inside a decoded
     * buffer, so compare by length + bytes. */
    expect_len = strlen(expected_engine);
    if (rec->engine_name_len != expect_len)
        return false;
    if (expect_len && memcmp(rec->engine_name, expected_engine, expect_len) != 0)
        return false;

    /* Freshness: reject future stamps (previous boot) and stale stamps. */
    if (rec->boottime_ms > now_boottime_ms)
        return false;
    if (now_boottime_ms - rec->boottime_ms > max_age_ms)
        return false;

    return true;
}
