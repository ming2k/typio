/**
 * @file candidate_popup_paint.h
 * @brief Cairo paint-and-commit path for the popup UI
 */

#ifndef TYPIO_WL_CANDIDATE_POPUP_PAINT_H
#define TYPIO_WL_CANDIDATE_POPUP_PAINT_H

#include "candidate_popup_buffer.h"
#include "candidate_popup_layout.h"
#include "candidate_popup_theme.h"

#include <stdbool.h>
#include <stddef.h>

struct wl_shm;
struct wl_surface;

typedef struct TypioCandidatePopupPaintTarget {
    struct wl_surface *surface;
    struct wl_shm *shm;
    TypioCandidatePopupBuffer *buffers;
    size_t buffer_count;
} TypioCandidatePopupPaintTarget;

bool typio_candidate_popup_paint_and_commit(const TypioCandidatePopupPaintTarget *target,
                                  TypioCandidatePopupFontCache *font_cache,
                                  const TypioCandidatePopupLine *lines,
                                  size_t line_count,
                                  int selected,
                                  const char *preedit_text,
                                  int width, int height,
                                  int scale,
                                  const TypioCandidatePopupRenderConfig *config,
                                  const TypioCandidatePopupPalette *palette);

#endif /* TYPIO_WL_CANDIDATE_POPUP_PAINT_H */
