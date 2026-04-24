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

fx_context *typio_flux_context_get(void);
fx_color typio_flux_color(TypioColor color);

bool typio_flux_draw_layout(fx_canvas *canvas,
                            TypioTextLayout *layout,
                            float x,
                            float y);
void typio_flux_layout_free(TypioTextLayout *layout);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_FLUX_RENDERER_H */
