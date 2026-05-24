/**
 * @file checkpoint_codec.h
 * @brief Pure encode/decode/validate for the session checkpoint format
 *
 * A checkpoint wraps an engine-produced session blob with the metadata
 * needed to decide, on the next daemon start, whether the blob is safe to
 * replay: a magic+version (reject incompatible builds), the engine name
 * (only restore into the same engine), an optional client identity
 * (best-effort same-app match), and a CLOCK_BOOTTIME stamp.
 *
 * Using a boottime stamp is deliberate: it makes one comparison serve two
 * purposes. A stamp close to the current boottime is fresh; a stamp larger
 * than the current boottime (or far in the past) is from a previous boot
 * and is rejected — exactly what we want, since a reboot means the
 * composition is long gone.
 *
 * This unit is dependency-free (header + stdlib only) so the wire format
 * can be unit-tested without files, engines, or the frontend.
 */

#ifndef TYPIO_WL_CHECKPOINT_CODEC_H
#define TYPIO_WL_CHECKPOINT_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TYPIO_CKP_MAGIC "TYPIOCK1"   /* 8 bytes, no NUL stored */
#define TYPIO_CKP_MAGIC_LEN 8u
#define TYPIO_CKP_VERSION 1u

/*
 * On encode the string fields are read as NUL-terminated C strings. On
 * decode they point INTO the source buffer, which contains no interior
 * NULs between fields — so decoded consumers must use the *_len companions
 * rather than treating the pointers as NUL-terminated. The *_len fields
 * are ignored by encode (it uses strlen) and populated by decode.
 */
typedef struct {
    uint32_t version;
    const char *engine_name; /* borrowed */
    size_t engine_name_len;  /* set by decode; ignored by encode */
    const char *identity;    /* borrowed; may be "" */
    size_t identity_len;     /* set by decode; ignored by encode */
    uint64_t boottime_ms;
    const char *blob;        /* borrowed; NULL when blob_size == 0 */
    size_t blob_size;
} TypioCkpRecord;

/**
 * Encode @c rec into a freshly malloc'd buffer. Returns the buffer and
 * sets @c *out_size, or NULL on allocation failure or invalid input
 * (NULL engine_name). Caller frees with free().
 */
char *typio_ckp_encode(const TypioCkpRecord *rec, size_t *out_size);

/**
 * Decode @c buf into @c out. The string/blob fields of @c out point INTO
 * @c buf, so @c buf must outlive @c out. Returns true only for a
 * well-formed buffer (good magic, sane lengths that fit within @c size);
 * false on any truncation or corruption. Never reads past @c size.
 */
bool typio_ckp_decode(const char *buf, size_t size, TypioCkpRecord *out);

/**
 * Pure validity gate for a decoded record: version equals
 * TYPIO_CKP_VERSION, engine_name equals @c expected_engine, and
 * boottime_ms lies within (now_boottime_ms - max_age_ms, now_boottime_ms].
 * A stamp in the future or older than the window (e.g. a previous boot) is
 * rejected.
 */
bool typio_ckp_is_valid(const TypioCkpRecord *rec,
                        const char *expected_engine,
                        uint64_t now_boottime_ms,
                        uint64_t max_age_ms);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_CHECKPOINT_CODEC_H */
