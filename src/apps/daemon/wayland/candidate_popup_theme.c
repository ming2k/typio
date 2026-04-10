/**
 * @file candidate_popup_theme.c
 * @brief Theme detection and color palettes for popup UI
 *
 * Detects the desktop dark/light preference by checking (in order):
 *   1. GTK_THEME environment variable
 *   2. ~/.config/gtk-4.0/settings.ini
 *   3. ~/.config/gtk-3.0/settings.ini
 *   4. ~/.config/kdeglobals (KDE ColorScheme)
 *
 * The resolved palette is cached with a 5-second TTL to avoid repeated
 * filesystem reads during rapid rendering cycles.
 */

#include "candidate_popup_theme.h"
#include "monotonic_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TYPIO_CANDIDATE_POPUP_THEME_CACHE_MS 5000

/* ── Built-in palettes ──────────────────────────────────────────────── */

static const TypioCandidatePopupPalette palette_light = {
    .bg_r = 0.985, .bg_g = 0.988, .bg_b = 0.992, .bg_a = 0.985,
    .border_r = 0.82, .border_g = 0.85, .border_b = 0.89, .border_a = 1.0,
    .text_r = 0.10, .text_g = 0.12, .text_b = 0.15,
    .muted_r = 0.36, .muted_g = 0.40, .muted_b = 0.46,
    .preedit_r = 0.28, .preedit_g = 0.31, .preedit_b = 0.36,
    .selection_r = 0.11, .selection_g = 0.34, .selection_b = 0.82, .selection_a = 0.97,
    .selection_text_r = 1.0, .selection_text_g = 1.0, .selection_text_b = 1.0,
};

static const TypioCandidatePopupPalette palette_dark = {
    .bg_r = 0.085, .bg_g = 0.095, .bg_b = 0.11, .bg_a = 0.985,
    .border_r = 0.23, .border_g = 0.26, .border_b = 0.31, .border_a = 1.0,
    .text_r = 0.93, .text_g = 0.95, .text_b = 0.97,
    .muted_r = 0.68, .muted_g = 0.71, .muted_b = 0.76,
    .preedit_r = 0.80, .preedit_g = 0.82, .preedit_b = 0.86,
    .selection_r = 0.20, .selection_g = 0.44, .selection_b = 0.95, .selection_a = 0.97,
    .selection_text_r = 1.0, .selection_text_g = 1.0, .selection_text_b = 1.0,
};


static bool str_contains_dark(const char *value) {
    if (!value || !*value) {
        return false;
    }

    return strstr(value, "dark") != nullptr || strstr(value, "Dark") != nullptr;
}

static bool config_file_has_needle(const char *path, const char *needle) {
    FILE *file;
    char line[512];

    if (!path || !needle) {
        return false;
    }

    file = fopen(path, "r");
    if (!file) {
        return false;
    }

    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, needle) != nullptr) {
            fclose(file);
            return true;
        }
    }

    fclose(file);
    return false;
}

static bool kde_prefers_dark(void) {
    const char *home = getenv("HOME");
    char path[512];
    FILE *file;
    char line[512];
    bool in_general = false;

    if (!home || !*home) {
        return false;
    }

    snprintf(path, sizeof(path), "%s/.config/kdeglobals", home);
    file = fopen(path, "r");
    if (!file) {
        return false;
    }

    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '[') {
            in_general = strstr(line, "[General]") != nullptr;
            continue;
        }

        if (in_general && strncmp(line, "ColorScheme=", 12) == 0) {
            char *value = line + 12;
            while (*value == ' ' || *value == '\t') {
                ++value;
            }
            fclose(file);
            return str_contains_dark(value);
        }
    }

    fclose(file);
    return false;
}

static bool desktop_prefers_dark(void) {
    const char *home = getenv("HOME");
    const char *gtk_theme = getenv("GTK_THEME");
    char path[512];

    if (str_contains_dark(gtk_theme)) {
        return true;
    }

    if (!home || !*home) {
        return false;
    }

    snprintf(path, sizeof(path), "%s/.config/gtk-4.0/settings.ini", home);
    if (config_file_has_needle(path, "gtk-application-prefer-dark-theme=1")) {
        return true;
    }

    snprintf(path, sizeof(path), "%s/.config/gtk-3.0/settings.ini", home);
    if (config_file_has_needle(path, "gtk-application-prefer-dark-theme=1")) {
        return true;
    }

    return kde_prefers_dark();
}

static const TypioCandidatePopupPalette *resolve_uncached(TypioCandidatePopupThemeMode mode) {
    switch (mode) {
        case TYPIO_CANDIDATE_POPUP_THEME_DARK:
            return &palette_dark;
        case TYPIO_CANDIDATE_POPUP_THEME_LIGHT:
            return &palette_light;
        case TYPIO_CANDIDATE_POPUP_THEME_AUTO:
        default:
            return desktop_prefers_dark() ? &palette_dark : &palette_light;
    }
}

const TypioCandidatePopupPalette *typio_candidate_popup_palette_light(void) {
    return &palette_light;
}

const TypioCandidatePopupPalette *typio_candidate_popup_palette_dark(void) {
    return &palette_dark;
}

const TypioCandidatePopupPalette *typio_candidate_popup_theme_resolve(
    TypioCandidatePopupThemeCache *cache, TypioCandidatePopupThemeMode mode) {
    uint64_t now = typio_wl_monotonic_ms();

    if (cache->palette && cache->mode == mode &&
        now - cache->resolved_at_ms < TYPIO_CANDIDATE_POPUP_THEME_CACHE_MS) {
        return cache->palette;
    }

    cache->palette = resolve_uncached(mode);
    cache->mode = mode;
    cache->resolved_at_ms = now;
    return cache->palette;
}

/* ── Utilities ──────────────────────────────────────────────────────── */

bool typio_parse_hex_color(const char *hex,
                            double *r, double *g, double *b, double *a) {
    unsigned int ri, gi, bi, ai = 255;
    int len;

    if (!hex || hex[0] != '#') return false;

    len = (int)strlen(hex + 1);
    if (len == 6) {
        if (sscanf(hex + 1, "%2x%2x%2x", &ri, &gi, &bi) != 3) return false;
    } else if (len == 8) {
        if (sscanf(hex + 1, "%2x%2x%2x%2x", &ri, &gi, &bi, &ai) != 4) return false;
    } else {
        return false;
    }

    if (r) *r = ri / 255.0;
    if (g) *g = gi / 255.0;
    if (b) *b = bi / 255.0;
    if (a) *a = ai / 255.0;
    return true;
}

uint64_t typio_candidate_popup_palette_hash(const TypioCandidatePopupPalette *p) {
    uint64_t h = 14695981039346656037ULL;
    const unsigned char *bytes = (const unsigned char *)p;
    size_t i;

    if (!p) return 0;

    for (i = 0; i < sizeof(*p); ++i) {
        h ^= bytes[i];
        h *= 1099511628211ULL;
    }
    return h;
}
