/**
 * @file candidate_popup_paint.h
 * @brief Paint paths for the candidate popup.
 *
 * All paths now perform a full redraw.  The previous delta-optimised paths
 * (selection-only / aux-only) have been simplified to full redraws because
 * the candidate popup is tiny; rebuilding the entire Vulkan offscreen
 * surface every frame was causing multi-second GPU stalls.
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
bool popup_paint_full(PopupRenderCtx *pc,
                      const PopupPaintTarget *target,
                      const PopupGeometry *geom,
                      int selected,
                      TypioCandidatePopupBuffer **out_buf);

/**
 * Selection change: performs a full redraw.  The old fast-path that blitted
 * the previous buffer has been removed because it required per-frame Vulkan
 * image creation + vkQueueWaitIdle, which caused 5-second GPU stalls.
 */
bool popup_paint_selection(PopupRenderCtx *pc,
                           const PopupPaintTarget *target,
                           const PopupGeometry *geom,
                           int old_sel,
                           int new_sel,
                           const TypioCandidatePopupBuffer *src,
                           TypioCandidatePopupBuffer **out_buf);

/**
 * Aux-only repaint: performs a full redraw.
 */
bool popup_paint_aux(PopupRenderCtx *pc,
                     const PopupPaintTarget *target,
                     const PopupGeometry *old_geom,
                     const PopupGeometry *new_geom,
                     int selected,
                     const TypioCandidatePopupBuffer *src,
                     TypioCandidatePopupBuffer **out_buf);

#endif /* TYPIO_WL_CANDIDATE_POPUP_PAINT_H */
