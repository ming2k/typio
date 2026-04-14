/**
 * @file candidate_popup_theme.h
 * @brief Theme detection and color palettes for popup UI
 */

#ifndef TYPIO_WL_CANDIDATE_POPUP_THEME_H
#define TYPIO_WL_CANDIDATE_POPUP_THEME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TypioCandidatePopupThemeMode {
    TYPIO_CANDIDATE_POPUP_THEME_AUTO = 0,  /* detect from desktop settings */
    TYPIO_CANDIDATE_POPUP_THEME_LIGHT,     /* always use built-in light    */
    TYPIO_CANDIDATE_POPUP_THEME_DARK,      /* always use built-in dark     */
} TypioCandidatePopupThemeMode;

typedef struct TypioCandidatePopupPalette {
    double bg_r, bg_g, bg_b, bg_a;
    double border_r, border_g, border_b, border_a;
    double text_r, text_g, text_b;
    double muted_r, muted_g, muted_b;      /* mode label and candidate index  */
    double preedit_r, preedit_g, preedit_b;
    double selection_r, selection_g, selection_b, selection_a;
    double selection_text_r, selection_text_g, selection_text_b;
} TypioCandidatePopupPalette;

typedef struct TypioCandidatePopupThemeCache {
    const TypioCandidatePopupPalette *palette;
    TypioCandidatePopupThemeMode mode;
    uint64_t resolved_at_ms;
} TypioCandidatePopupThemeCache;

/**
 * Resolve the palette for @mode, using the cache to avoid repeated
 * filesystem reads during rapid rendering cycles.
 */
const TypioCandidatePopupPalette *typio_candidate_popup_theme_resolve(
    TypioCandidatePopupThemeCache *cache, TypioCandidatePopupThemeMode mode);

/**
 * Return the built-in light palette pointer.
 * Usable as an identity check: (resolved == typio_candidate_popup_palette_dark()).
 */
const TypioCandidatePopupPalette *typio_candidate_popup_palette_light(void);
const TypioCandidatePopupPalette *typio_candidate_popup_palette_dark(void);

/**
 * Parse a "#rrggbb" or "#rrggbbaa" hex color string.
 * Alpha defaults to 1.0 for 6-digit form.
 * Returns true on success, false if the string is not a valid hex color.
 */
bool typio_parse_hex_color(const char *hex,
                            double *r, double *g, double *b, double *a);

/**
 * Compute a stable FNV-1a content hash of a palette, usable for change
 * detection without comparing raw pointers.
 */
uint64_t typio_candidate_popup_palette_hash(const TypioCandidatePopupPalette *p);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_CANDIDATE_POPUP_THEME_H */
