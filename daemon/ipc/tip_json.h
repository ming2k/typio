/**
 * @file tip_json.h
 * @brief Minimal JSON helpers for the Typio IPC Protocol.
 *
 * No external JSON library dependency.  Hand-written for the
 * fixed schemas that typio exchanges.  Not a general-purpose
 * parser.
 */

#ifndef TYPIO_TIP_JSON_H
#define TYPIO_TIP_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Read helpers  (ad-hoc string scanning)                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Extract a string value for @p key from a JSON object.
 * @return malloc'd NUL-terminated string, or NULL if not found / not a string.
 */
char *tip_json_extract_string(const char *json, const char *key);

/**
 * @brief Extract the integer "id" field.
 * @return true if found.
 */
bool tip_json_extract_id(const char *json, int *out_id);

/**
 * @brief Extract the raw value of the "params" key.
 *
 * Sets *out_start = NULL and *out_len = 0 when params is absent.
 * Otherwise points into the original @p json buffer (caller must copy
 * if the buffer is transient).
 */
bool tip_json_extract_params(const char *json,
                              const char **out_start,
                              size_t *out_len);

/**
 * @brief Extract a string array for @p key.
 *
 * Writes at most @p max_count entries into @p out_array.
 * @return number of entries written, or -1 on error.
 */
int tip_json_extract_string_array(const char *json,
                                   const char *key,
                                   char **out_array,
                                   int max_count);

/* ------------------------------------------------------------------ */
/*  Write helpers (growable buffer)                                   */
/* ------------------------------------------------------------------ */

typedef struct TipJsonBuilder TipJsonBuilder;

TipJsonBuilder *tip_json_builder_new(void);
void            tip_json_builder_free(TipJsonBuilder *b);

void tip_json_builder_append_raw(TipJsonBuilder *b, const char *s);
void tip_json_builder_append_string(TipJsonBuilder *b, const char *s);
void tip_json_builder_append_int(TipJsonBuilder *b, int v);
void tip_json_builder_append_uint32(TipJsonBuilder *b, uint32_t v);
void tip_json_builder_append_int32(TipJsonBuilder *b, int32_t v);
void tip_json_builder_append_bool(TipJsonBuilder *b, bool v);
void tip_json_builder_append_double(TipJsonBuilder *b, double v);
void tip_json_builder_append_null(TipJsonBuilder *b);

/**
 * @brief Take ownership of the internal buffer and reset the builder.
 * @return malloc'd string (may be empty).  Caller frees.
 */
char *tip_json_builder_steal(TipJsonBuilder *b);

/* ------------------------------------------------------------------ */
/*  High-level framing                                                */
/* ------------------------------------------------------------------ */

/** Build a JSON-RPC 2.0 success response.  Caller frees result. */
char *tip_json_build_response(int id, const char *result_json);

/** Build a JSON-RPC 2.0 error response.  Caller frees result. */
char *tip_json_build_error(int id, int code, const char *message);

/** Build a JSON-RPC 2.0 server→client notification.  Caller frees result. */
char *tip_json_build_notify(const char *method, const char *params_json);

/* ------------------------------------------------------------------ */
/*  Convenience macros for builder blocks                             */
/* ------------------------------------------------------------------ */

#define TIP_JSON_OBJ_START(b) tip_json_builder_append_raw((b), "{")
#define TIP_JSON_OBJ_END(b)   tip_json_builder_append_raw((b), "}")
#define TIP_JSON_ARR_START(b) tip_json_builder_append_raw((b), "[")
#define TIP_JSON_ARR_END(b)   tip_json_builder_append_raw((b), "]")
#define TIP_JSON_COMMA(b)     tip_json_builder_append_raw((b), ",")
#define TIP_JSON_KEY(b, k)    do { tip_json_builder_append_string((b), (k)); \
                                   tip_json_builder_append_raw((b), ":"); } while(0)

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_TIP_JSON_H */
