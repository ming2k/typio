/**
 * @file string.h
 * @brief String utilities (public ABI). Implementations live in the
 *        Rust crate (src/string.rs).
 */

#ifndef TYPIO_STRING_H
#define TYPIO_STRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Allocation */
char *typio_strdup(const char *str);
char *typio_strndup(const char *str, size_t n);

/* Composition */
char *typio_strjoin(const char *a, const char *b);
char *typio_strjoin3(const char *a, const char *b, const char *c);
char *typio_path_join(const char *base, const char *suffix);

/* Comparison */
bool typio_str_starts_with(const char *str, const char *prefix);
bool typio_str_ends_with(const char *str, const char *suffix);
bool typio_str_equals(const char *a, const char *b);
bool typio_str_equals_nocase(const char *a, const char *b);

/* Search */
const char *typio_str_find(const char *haystack, const char *needle);

/* Conversion */
int typio_str_to_int(const char *str, int default_val);
double typio_str_to_double(const char *str, double default_val);
bool typio_str_to_bool(const char *str, bool default_val);

/* UTF-8 */
size_t typio_utf8_strlen(const char *str);
const char *typio_utf8_next(const char *str);
const char *typio_utf8_prev(const char *str, const char *start);
uint32_t typio_utf8_get_char(const char *str);
size_t typio_utf8_encode(uint32_t codepoint, char *buf);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_STRING_H */
