/**
 * @file candidate_panel_theme.h
 * @brief Theme detection and color palettes for popup UI
 */

#ifndef TYPIO_WL_CANDIDATE_POPUP_THEME_H
#define TYPIO_WL_CANDIDATE_POPUP_THEME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TypioCandidatePanelThemeMode {
    TYPIO_CANDIDATE_POPUP_THEME_AUTO = 0,  /* detect from desktop settings */
    TYPIO_CANDIDATE_POPUP_THEME_LIGHT,     /* always use built-in light    */
    TYPIO_CANDIDATE_POPUP_THEME_DARK,      /* always use built-in dark     */
} TypioCandidatePanelThemeMode;

typedef struct TypioCandidatePanelPalette {
    double bg_r, bg_g, bg_b, bg_a;
    double border_r, border_g, border_b, border_a;
    double text_r, text_g, text_b;
    double muted_r, muted_g, muted_b;      /* mode label and candidate index  */
    double preedit_r, preedit_g, preedit_b;
    double selection_r, selection_g, selection_b, selection_a;
    double selection_text_r, selection_text_g, selection_text_b;
    /* Voice / IME status indicators share the preedit colour and the
     * preedit layout slot — there is no separate "status" treatment. */
} TypioCandidatePanelPalette;

typedef struct TypioCandidatePanelThemeCache {
    const TypioCandidatePanelPalette *palette;
    TypioCandidatePanelThemeMode mode;
    uint64_t resolved_at_ms;
} TypioCandidatePanelThemeCache;

/**
 * Resolve the palette for @mode, using the cache to avoid repeated
 * filesystem reads during rapid rendering cycles.
 */
const TypioCandidatePanelPalette *typio_candidate_panel_theme_resolve(
    TypioCandidatePanelThemeCache *cache, TypioCandidatePanelThemeMode mode);

/**
 * Return the built-in light palette pointer.
 * Usable as an identity check: (resolved == typio_candidate_panel_palette_dark()).
 */
const TypioCandidatePanelPalette *typio_candidate_panel_palette_light(void);
const TypioCandidatePanelPalette *typio_candidate_panel_palette_dark(void);

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
uint64_t typio_candidate_panel_palette_hash(const TypioCandidatePanelPalette *p);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_CANDIDATE_POPUP_THEME_H */
