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

flux_device *typio_flux_device_get(void);
flux_color   typio_flux_color(TypioColor color);

/*
 * Rasterize layout into a CPU pixel buffer (ARGB8888, row-major).
 *
 * pixel_buf : pointer to the start of the buffer
 * stride    : bytes per row
 * buf_h     : buffer height in pixels (for bounds checking)
 * x, y      : top-left of the text baseline origin in pixels
 */
bool typio_flux_draw_layout(void *pixel_buf, int stride, int buf_h,
                            TypioTextLayout *layout,
                            float x, float y);

void typio_flux_layout_free(TypioTextLayout *layout);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_FLUX_RENDERER_H */
