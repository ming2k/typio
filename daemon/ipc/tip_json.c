/**
 * @file tip_json.c
 * @brief Minimal JSON read/write implementation.
 */

#include "tip_json.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/*  Read helpers                                                      */
/* ================================================================== */

static const char *skip_ws(const char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

char *tip_json_extract_string(const char *json, const char *key)
{
    char pattern[128];
    const char *cursor;
    char *result;
    size_t len;
    bool escaped;

    if (!json || !key || strlen(key) > 100)
        return NULL;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    cursor = strstr(json, pattern);
    if (!cursor)
        return NULL;

    cursor += strlen(pattern);
    cursor = skip_ws(cursor);
    if (*cursor != ':')
        return NULL;
    cursor++;
    cursor = skip_ws(cursor);
    if (*cursor != '"')
        return NULL;
    cursor++;

    escaped = false;
    len = 0;
    while (cursor[len] && (cursor[len] != '"' || escaped)) {
        escaped = (cursor[len] == '\\' && !escaped);
        len++;
    }
    if (cursor[len] != '"')
        return NULL;

    result = malloc(len + 1);
    if (!result)
        return NULL;
    memcpy(result, cursor, len);
    result[len] = '\0';
    return result;
}

bool tip_json_extract_id(const char *json, int *out_id)
{
    const char *cursor;

    if (!json || !out_id)
        return false;

    cursor = strstr(json, "\"id\"");
    if (!cursor)
        return false;
    cursor += 4;
    cursor = skip_ws(cursor);
    if (*cursor != ':')
        return false;
    cursor++;
    cursor = skip_ws(cursor);
    *out_id = (int)strtol(cursor, NULL, 10);
    return true;
}

bool tip_json_extract_params(const char *json,
                              const char **out_start,
                              size_t *out_len)
{
    const char *cursor;

    if (!json || !out_start || !out_len)
        return false;

    cursor = strstr(json, "\"params\"");
    if (!cursor) {
        *out_start = NULL;
        *out_len = 0;
        return true;
    }
    cursor += 8;
    cursor = skip_ws(cursor);
    if (*cursor != ':')
        return false;
    cursor++;
    cursor = skip_ws(cursor);

    if (*cursor == '{') {
        int depth = 1;
        const char *start = cursor;
        cursor++;
        while (*cursor && depth > 0) {
            if (*cursor == '"') {
                cursor++;
                while (*cursor && (*cursor != '"' || cursor[-1] == '\\'))
                    cursor++;
                if (*cursor == '"')
                    cursor++;
            } else {
                if (*cursor == '{')
                    depth++;
                else if (*cursor == '}')
                    depth--;
                cursor++;
            }
        }
        *out_start = start;
        *out_len = (size_t)(cursor - start);
        return true;
    }

    /* null or scalar */
    {
        const char *start = cursor;
        while (*cursor && *cursor != ',' && *cursor != '}')
            cursor++;
        *out_start = start;
        *out_len = (size_t)(cursor - start);
        return true;
    }
}

int tip_json_extract_string_array(const char *json,
                                   const char *key,
                                   char **out_array,
                                   int max_count)
{
    char pattern[128];
    const char *cursor;
    int count = 0;

    if (!json || !key || !out_array || max_count <= 0)
        return -1;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    cursor = strstr(json, pattern);
    if (!cursor)
        return -1;
    cursor += strlen(pattern);
    cursor = skip_ws(cursor);
    if (*cursor != ':')
        return -1;
    cursor++;
    cursor = skip_ws(cursor);
    if (*cursor != '[')
        return -1;
    cursor++;

    while (*cursor) {
        cursor = skip_ws(cursor);
        if (*cursor == ']')
            break;
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor == '"') {
            const char *start;
            size_t len = 0;
            bool escaped = false;
            cursor++;
            start = cursor;
            while (*cursor && (*cursor != '"' || escaped)) {
                escaped = (*cursor == '\\' && !escaped);
                cursor++;
                len++;
            }
            if (*cursor == '"')
                cursor++;
            if (count < max_count) {
                out_array[count] = malloc(len + 1);
                if (!out_array[count])
                    return -1;
                memcpy(out_array[count], start, len);
                out_array[count][len] = '\0';
                count++;
            }
        } else {
            /* Skip non-string element */
            while (*cursor && *cursor != ',' && *cursor != ']')
                cursor++;
        }
    }
    return count;
}

/* ================================================================== */
/*  Write helpers                                                     */
/* ================================================================== */

struct TipJsonBuilder {
    char *buf;
    size_t len;
    size_t cap;
};

TipJsonBuilder *tip_json_builder_new(void)
{
    TipJsonBuilder *b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    b->cap = 256;
    b->buf = malloc(b->cap);
    if (!b->buf) {
        free(b);
        return NULL;
    }
    b->buf[0] = '\0';
    return b;
}

void tip_json_builder_free(TipJsonBuilder *b)
{
    if (!b)
        return;
    free(b->buf);
    free(b);
}

static void ensure_cap(TipJsonBuilder *b, size_t need)
{
    if (b->len + need >= b->cap) {
        size_t new_cap = b->cap * 2;
        while (new_cap < b->len + need + 1)
            new_cap *= 2;
        b->buf = realloc(b->buf, new_cap);
        b->cap = new_cap;
    }
}

void tip_json_builder_append_raw(TipJsonBuilder *b, const char *s)
{
    size_t n;
    if (!b || !s)
        return;
    n = strlen(s);
    ensure_cap(b, n + 1);
    memcpy(b->buf + b->len, s, n + 1);
    b->len += n;
}

void tip_json_builder_append_string(TipJsonBuilder *b, const char *s)
{
    if (!b)
        return;
    tip_json_builder_append_raw(b, "\"");
    if (s) {
        for (const char *p = s; *p; p++) {
            switch (*p) {
            case '"':
                tip_json_builder_append_raw(b, "\\\"");
                break;
            case '\\':
                tip_json_builder_append_raw(b, "\\\\");
                break;
            case '\b':
                tip_json_builder_append_raw(b, "\\b");
                break;
            case '\f':
                tip_json_builder_append_raw(b, "\\f");
                break;
            case '\n':
                tip_json_builder_append_raw(b, "\\n");
                break;
            case '\r':
                tip_json_builder_append_raw(b, "\\r");
                break;
            case '\t':
                tip_json_builder_append_raw(b, "\\t");
                break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char esc[7];
                    snprintf(esc, sizeof(esc), "\\u%04x",
                             (unsigned char)*p);
                    tip_json_builder_append_raw(b, esc);
                } else {
                    char ch[2] = { *p, '\0' };
                    tip_json_builder_append_raw(b, ch);
                }
            }
        }
    }
    tip_json_builder_append_raw(b, "\"");
}

void tip_json_builder_append_int(TipJsonBuilder *b, int v)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", v);
    tip_json_builder_append_raw(b, tmp);
}

void tip_json_builder_append_uint32(TipJsonBuilder *b, uint32_t v)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%u", v);
    tip_json_builder_append_raw(b, tmp);
}

void tip_json_builder_append_int32(TipJsonBuilder *b, int32_t v)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%" PRId32, v);
    tip_json_builder_append_raw(b, tmp);
}

void tip_json_builder_append_bool(TipJsonBuilder *b, bool v)
{
    tip_json_builder_append_raw(b, v ? "true" : "false");
}

void tip_json_builder_append_double(TipJsonBuilder *b, double v)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%g", v);
    tip_json_builder_append_raw(b, tmp);
}

void tip_json_builder_append_null(TipJsonBuilder *b)
{
    tip_json_builder_append_raw(b, "null");
}

char *tip_json_builder_steal(TipJsonBuilder *b)
{
    char *s;
    if (!b)
        return NULL;
    s = b->buf;
    /* Ownership of the buffer transfers to the caller; the builder struct is
     * discarded (steal == take buffer + drop builder). Freeing it here avoids
     * leaking the struct at every call site. */
    free(b);
    return s;
}

/* ================================================================== */
/*  High-level framing                                                */
/* ================================================================== */

char *tip_json_build_response(int id, const char *result_json)
{
    TipJsonBuilder *b = tip_json_builder_new();
    if (!b)
        return NULL;
    tip_json_builder_append_raw(b, "{\"jsonrpc\":\"2.0\",\"id\":");
    tip_json_builder_append_int(b, id);
    tip_json_builder_append_raw(b, ",\"result\":");
    if (result_json)
        tip_json_builder_append_raw(b, result_json);
    else
        tip_json_builder_append_raw(b, "null");
    tip_json_builder_append_raw(b, "}");
    return tip_json_builder_steal(b);
}

char *tip_json_build_error(int id, int code, const char *message)
{
    TipJsonBuilder *b = tip_json_builder_new();
    if (!b)
        return NULL;
    tip_json_builder_append_raw(b, "{\"jsonrpc\":\"2.0\",\"id\":");
    tip_json_builder_append_int(b, id);
    tip_json_builder_append_raw(b, ",\"error\":{\"code\":");
    tip_json_builder_append_int(b, code);
    tip_json_builder_append_raw(b, ",\"message\":");
    tip_json_builder_append_string(b, message);
    tip_json_builder_append_raw(b, "}}");
    return tip_json_builder_steal(b);
}

char *tip_json_build_notify(const char *method, const char *params_json)
{
    TipJsonBuilder *b = tip_json_builder_new();
    if (!b)
        return NULL;
    tip_json_builder_append_raw(b, "{\"jsonrpc\":\"2.0\",\"method\":");
    tip_json_builder_append_string(b, method);
    tip_json_builder_append_raw(b, ",\"params\":");
    if (params_json)
        tip_json_builder_append_raw(b, params_json);
    else
        tip_json_builder_append_raw(b, "{}");
    tip_json_builder_append_raw(b, "}");
    return tip_json_builder_steal(b);
}
