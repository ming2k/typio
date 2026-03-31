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
                                  const TypioCandidatePopupLine *lines,
                                  size_t line_count,
                                  int selected,
                                  PangoLayout *preedit_layout,
                                  int preedit_x,
                                  int preedit_y,
                                  PangoLayout *mode_label_layout,
                                  int mode_label_x,
                                  int mode_label_y,
                                  int mode_label_divider_y,
                                  int width, int height,
                                  int scale,
                                  const TypioCandidatePopupPalette *palette,
                                  TypioCandidatePopupBuffer **out_buffer);

bool typio_candidate_popup_paint_selection_update(
                                  const TypioCandidatePopupPaintTarget *target,
                                  const TypioCandidatePopupLine *lines,
                                  size_t line_count,
                                  int old_selected,
                                  int new_selected,
                                  int width, int height,
                                  int scale,
                                  const TypioCandidatePopupPalette *palette,
                                  const TypioCandidatePopupBuffer *source_buffer,
                                  TypioCandidatePopupBuffer **out_buffer);

bool typio_candidate_popup_paint_aux_update(
                                  const TypioCandidatePopupPaintTarget *target,
                                  int width, int height,
                                  int scale,
                                  const TypioCandidatePopupPalette *palette,
                                  const TypioCandidatePopupBuffer *source_buffer,
                                  PangoLayout *old_preedit_layout,
                                  int old_preedit_x,
                                  int old_preedit_y,
                                  int old_preedit_width,
                                  int old_preedit_height,
                                  PangoLayout *new_preedit_layout,
                                  int new_preedit_x,
                                  int new_preedit_y,
                                  int new_preedit_width,
                                  int new_preedit_height,
                                  PangoLayout *old_mode_label_layout,
                                  int old_mode_label_x,
                                  int old_mode_label_y,
                                  int old_mode_label_width,
                                  int old_mode_label_height,
                                  int old_mode_label_divider_y,
                                  PangoLayout *new_mode_label_layout,
                                  int new_mode_label_x,
                                  int new_mode_label_y,
                                  int new_mode_label_width,
                                  int new_mode_label_height,
                                  int new_mode_label_divider_y,
                                  TypioCandidatePopupBuffer **out_buffer);

#endif /* TYPIO_WL_CANDIDATE_POPUP_PAINT_H */
