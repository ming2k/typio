#ifndef TYPIO_WL_FLUX_RENDERER_H
#define TYPIO_WL_FLUX_RENDERER_H

#include "typio/renderer.h"

#include <flux/flux.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

TypioTextEngine *typio_flux_engine_create(void);
void typio_flux_engine_destroy(TypioTextEngine *engine);

/* Shared, lazily-created flux device.
 *
 * Non-headless: created with the Wayland WSI instance extensions and the
 * swapchain device extension so the candidate popup can present a Vulkan
 * swapchain directly onto its zwp_input_popup_surface_v2 wl_surface.
 * Returns NULL if no Vulkan device is available. */
flux_device *typio_flux_device_get(void);

/*
 * Record a shaped text layout into a flux canvas as filled glyph paths.
 *
 * Each glyph's FreeType outline is decomposed into a flux_path (allocated
 * from `arena`) and filled with the layout's colour. Must be called between
 * flux_canvas_begin / flux_canvas_end on `canvas`.
 *
 * x, y : top-left origin of the layout in surface pixels (baseline is added
 *        internally from the layout metrics).
 */
bool typio_flux_fill_layout(flux_canvas *canvas, flux_arena *arena,
                            TypioTextLayout *layout, float x, float y);

void typio_flux_layout_free(TypioTextLayout *layout);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_FLUX_RENDERER_H */
