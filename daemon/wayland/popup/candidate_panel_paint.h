/**
 * @file candidate_panel_paint.h
 * @brief Record the candidate panel into a flux canvas.
 *
 * The popup is drawn entirely on the GPU via flux: the background is the
 * canvas clear colour, rectangles (border, selection highlight, mode
 * divider) are solid fills, and text is filled glyph outlines. The popup
 * coordinator owns the frame lifecycle (begin_frame / submit / present);
 * this module only records draw commands between flux_canvas_begin/end.
 */

#ifndef TYPIO_WL_CANDIDATE_POPUP_PAINT_H
#define TYPIO_WL_CANDIDATE_POPUP_PAINT_H

#include "candidate_panel_layout.h"

#include <flux/flux.h>

typedef struct {
    flux_canvas *canvas;   /* recording target (between begin/end) */
    flux_arena  *arena;    /* per-frame arena for glyph paths */
} PopupPaintTarget;

/**
 * Record the full popup — border, candidate rows, selection highlight,
 * preedit, and mode label — into the canvas. The background is expected to
 * already be cleared to the palette background by the caller.
 */
void popup_record(const PopupPaintTarget *target,
                  const PopupGeometry *geom,
                  int selected);

#endif /* TYPIO_WL_CANDIDATE_POPUP_PAINT_H */
