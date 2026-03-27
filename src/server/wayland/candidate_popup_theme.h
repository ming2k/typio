/**
 * @file candidate_popup_theme.h
 * @brief Theme detection and color palettes for popup UI
 */

#ifndef TYPIO_WL_CANDIDATE_POPUP_THEME_H
#define TYPIO_WL_CANDIDATE_POPUP_THEME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum TypioCandidatePopupThemeMode {
    TYPIO_CANDIDATE_POPUP_THEME_AUTO = 0,
    TYPIO_CANDIDATE_POPUP_THEME_LIGHT,
    TYPIO_CANDIDATE_POPUP_THEME_DARK,
} TypioCandidatePopupThemeMode;

typedef struct TypioCandidatePopupPalette {
    double bg_r, bg_g, bg_b, bg_a;
    double border_r, border_g, border_b, border_a;
    double text_r, text_g, text_b;
    double muted_r, muted_g, muted_b;
    double preedit_r, preedit_g, preedit_b;
    double selection_r, selection_g, selection_b, selection_a;
    double selection_text_r, selection_text_g, selection_text_b;
} TypioCandidatePopupPalette;

typedef struct TypioCandidatePopupThemeCache {
    const TypioCandidatePopupPalette *palette;
    TypioCandidatePopupThemeMode mode;
    uint64_t resolved_at_ms;
} TypioCandidatePopupThemeCache;

const TypioCandidatePopupPalette *typio_candidate_popup_theme_resolve(TypioCandidatePopupThemeCache *cache,
                                                    TypioCandidatePopupThemeMode mode);

#endif /* TYPIO_WL_CANDIDATE_POPUP_THEME_H */
