/**
 * @file candidate_popup_paint.h
 * @brief Skia paint paths for the candidate popup.
 *
 * Three render paths, each taking a PopupGeometry snapshot:
 *
 *   popup_paint_full      — full background + border + all rows + aux text
 *   popup_paint_selection — blit previous buffer, repaint two rows only
 *   popup_paint_aux       — blit previous buffer, repaint preedit/mode regions
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

typedef struct {
    struct wl_surface             *surface;
    struct wl_shm                 *shm;
    TypioCandidatePopupBuffer     *buffers;
    size_t                         buffer_count;
} PopupPaintTarget;

/**
 * Full repaint: allocate/reuse buffer, paint background, border, all rows,
 * preedit, and mode label.  Commits the surface.
 */
bool popup_paint_full(const PopupPaintTarget *target,
                      const PopupGeometry *geom,
                      int selected,
                      TypioCandidatePopupBuffer **out_buf);

/**
 * Selection-only repaint: copy @src into a free buffer, then repaint only
 * the old and new selected rows.  Returns false if @src dimensions do not
 * match the geometry (caller should fall back to popup_paint_full).
 */
bool popup_paint_selection(const PopupPaintTarget *target,
                           const PopupGeometry *geom,
                           int old_sel,
                           int new_sel,
                           const TypioCandidatePopupBuffer *src,
                           TypioCandidatePopupBuffer **out_buf);

/**
 * Aux-only repaint: copy @src, erase old preedit/mode regions, draw new ones.
 * @old_geom and @new_geom must have the same popup_w/popup_h.
 */
bool popup_paint_aux(const PopupPaintTarget *target,
                     const PopupGeometry *old_geom,
                     const PopupGeometry *new_geom,
                     const TypioCandidatePopupBuffer *src,
                     TypioCandidatePopupBuffer **out_buf);

#endif /* TYPIO_WL_CANDIDATE_POPUP_PAINT_H */
