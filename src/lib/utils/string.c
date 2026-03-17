/**
 * @file string.c
 * @brief String utilities implementation
 */

#include "string.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

char *typio_strdup(const char *str) {
    if (!str) {
        return nullptr;
    }
    size_t len = strlen(str);
    char *copy = malloc(len + 1);
    if (copy) {
        memcpy(copy, str, len + 1);
    }
    return copy;
}

char *typio_strndup(const char *str, size_t n) {
    if (!str) {
        return nullptr;
    }
    size_t len = strlen(str);
    if (len > n) {
        len = n;
    }
    char *copy = malloc(len + 1);
    if (copy) {
        memcpy(copy, str, len);
        copy[len] = '\0';
    }
    return copy;
}

char *typio_strjoin(const char *a, const char *b) {
    if (!a && !b) return nullptr;
    if (!a) return typio_strdup(b);
    if (!b) return typio_strdup(a);

    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    char *result = malloc(len_a + len_b + 1);
    if (result) {
        memcpy(result, a, len_a);
        memcpy(result + len_a, b, len_b + 1);
    }
    return result;
}

char *typio_strjoin3(const char *a, const char *b, const char *c) {
    char *ab = typio_strjoin(a, b);
    if (!ab) return nullptr;
    char *result = typio_strjoin(ab, c);
    free(ab);
    return result;
}

char *typio_path_join(const char *base, const char *suffix) {
    size_t base_len;
    size_t suffix_len;
    bool need_slash;
    char *path;

    if (!base || !suffix) {
        return nullptr;
    }

    base_len = strlen(base);
    suffix_len = strlen(suffix);
    need_slash = base_len > 0 && base[base_len - 1] != '/';
    path = malloc(base_len + suffix_len + (need_slash ? 2U : 1U));
    if (!path) {
        return nullptr;
    }

    snprintf(path,
             base_len + suffix_len + (need_slash ? 2U : 1U),
             need_slash ? "%s/%s" : "%s%s",
             base,
             suffix);
    return path;
}

bool typio_str_starts_with(const char *str, const char *prefix) {
    if (!str || !prefix) return false;
    size_t prefix_len = strlen(prefix);
    return strncmp(str, prefix, prefix_len) == 0;
}

bool typio_str_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return false;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return false;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

bool typio_str_equals(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

bool typio_str_equals_nocase(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    }
    return *a == *b;
}

const char *typio_str_find(const char *haystack, const char *needle) {
    if (!haystack || !needle) return nullptr;
    return strstr(haystack, needle);
}

int typio_str_to_int(const char *str, int default_val) {
    if (!str) return default_val;
    char *end;
    long val = strtol(str, &end, 10);
    if (end == str) return default_val;
    return (int)val;
}

double typio_str_to_double(const char *str, double default_val) {
    if (!str) return default_val;
    char *end;
    double val = strtod(str, &end);
    if (end == str) return default_val;
    return val;
}

bool typio_str_to_bool(const char *str, bool default_val) {
    if (!str) return default_val;
    if (typio_str_equals_nocase(str, "true") ||
        typio_str_equals_nocase(str, "yes") ||
        typio_str_equals_nocase(str, "1") ||
        typio_str_equals_nocase(str, "on")) {
        return true;
    }
    if (typio_str_equals_nocase(str, "false") ||
        typio_str_equals_nocase(str, "no") ||
        typio_str_equals_nocase(str, "0") ||
        typio_str_equals_nocase(str, "off")) {
        return false;
    }
    return default_val;
}

/* UTF-8 utilities */

size_t typio_utf8_strlen(const char *str) {
    if (!str) return 0;

    size_t count = 0;
    while (*str) {
        if ((*str & 0xC0) != 0x80) {
            count++;
        }
        str++;
    }
    return count;
}

const char *typio_utf8_next(const char *str) {
    if (!str || !*str) return str;

    /* Skip first byte */
    str++;

    /* Skip continuation bytes */
    while (*str && (*str & 0xC0) == 0x80) {
        str++;
    }

    return str;
}

const char *typio_utf8_prev(const char *str, const char *start) {
    if (!str || !start || str <= start) return start;

    str--;
    while (str > start && (*str & 0xC0) == 0x80) {
        str--;
    }

    return str;
}

uint32_t typio_utf8_get_char(const char *str) {
    if (!str || !*str) return 0;

    unsigned char c = (unsigned char)*str;

    if (c < 0x80) {
        return c;
    }

    uint32_t result;
    int remaining;

    if ((c & 0xE0) == 0xC0) {
        result = c & 0x1F;
        remaining = 1;
    } else if ((c & 0xF0) == 0xE0) {
        result = c & 0x0F;
        remaining = 2;
    } else if ((c & 0xF8) == 0xF0) {
        result = c & 0x07;
        remaining = 3;
    } else {
        return 0xFFFD; /* Replacement character */
    }

    for (int i = 0; i < remaining; i++) {
        str++;
        if (!*str || (*str & 0xC0) != 0x80) {
            return 0xFFFD;
        }
        result = (result << 6) | (*str & 0x3F);
    }

    return result;
}

size_t typio_utf8_encode(uint32_t codepoint, char *buf) {
    if (!buf) return 0;

    if (codepoint < 0x80) {
        buf[0] = (char)codepoint;
        return 1;
    }

    if (codepoint < 0x800) {
        buf[0] = (char)(0xC0 | (codepoint >> 6));
        buf[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }

    if (codepoint < 0x10000) {
        buf[0] = (char)(0xE0 | (codepoint >> 12));
        buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }

    if (codepoint < 0x110000) {
        buf[0] = (char)(0xF0 | (codepoint >> 18));
        buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }

    /* Invalid codepoint */
    return 0;
}
