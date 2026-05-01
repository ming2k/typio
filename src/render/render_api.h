/**
 * @file render_api.h
 * @brief Pure C facade over the C++ Vulkan/Flux rendering pipeline.
 *
 * This header is the only interface that C code in src/apps/ should include.
 * It completely hides C++ types (fx_canvas, HarfBuzz, etc.) so that the main
 * daemon can be linked without pulling in a C++ runtime.
 */

#ifndef TYPIO_RENDER_API_H
#define TYPIO_RENDER_API_H

#include "typio/types.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to the rendering context (holds Vulkan surface, layout cache) */
typedef struct TypioRenderContext TypioRenderContext;

/* Handle to a formatted text layout (HarfBuzz-backed) */
typedef struct TypioTextLayout TypioTextLayout;

/* Geometry snapshot produced by the layout engine */
typedef struct TypioPopupGeometry TypioPopupGeometry;

typedef struct {
    float r, g, b, a;
} TypioRenderColor;

typedef struct {
    int x, y, w, h;
} TypioRenderRect;

typedef struct {
    TypioTextLayout *label_layout;
    TypioTextLayout *label_layout_sel;
    TypioTextLayout *text_layout;
    TypioTextLayout *text_layout_sel;
    int x, y, w, h;
    int label_x, label_y;
    int text_x, text_y;
} TypioPopupRow;

typedef struct {
    TypioPopupRow *rows;
    size_t row_count;
    int selected_index;
    int total_width, total_height;
    TypioTextLayout *mode_layout;
    int mode_divider_y;
} TypioPopupGeometry;

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

TypioRenderContext *typio_render_context_create(void);
void                typio_render_context_destroy(TypioRenderContext **ctx);

/* -------------------------------------------------------------------------- */
/* Text layout                                                                */
/* -------------------------------------------------------------------------- */

TypioTextLayout *typio_render_text_layout_create(TypioRenderContext *ctx,
                                                  const char *text,
                                                  const char *font_family,
                                                  int font_size_pt,
                                                  TypioRenderColor color);
void             typio_render_text_layout_free(TypioTextLayout **layout);
int              typio_render_text_layout_width(const TypioTextLayout *layout);
int              typio_render_text_layout_height(const TypioTextLayout *layout);

/* -------------------------------------------------------------------------- */
/* Geometry / paint                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Compute candidate-popup geometry from engine-provided state.
 *
 * @param ctx          Render context (owns the LRU layout cache).
 * @param candidates   Candidate strings.
 * @param candidate_count
 * @param labels       Selection labels (e.g. "1", "2").
 * @param selected     Currently selected index.
 * @param mode_text    Optional mode label (may be NULL).
 * @param scale        Output scale factor (1, 2, ...).
 * @param out_geom     Caller-allocated geometry structure to fill.
 * @return true on success.
 */
bool typio_render_popup_geometry(TypioRenderContext *ctx,
                                  const char **candidates,
                                  size_t candidate_count,
                                  const char **labels,
                                  int selected,
                                  const char *mode_text,
                                  int scale,
                                  TypioPopupGeometry *out_geom);

/**
 * @brief Paint the popup into a Wayland SHM buffer.
 *
 * @param ctx        Render context.
 * @param wl_shm     Wayland shm global.
 * @param width      Buffer width in pixels.
 * @param height     Buffer height in pixels.
 * @param geom       Geometry snapshot.
 * @param palette    Color palette.
 * @param out_buffer On success, set to the created wl_buffer.
 * @param out_data   On success, set to the mapped shm data.
 * @return true on success.
 */
bool typio_render_popup_paint(TypioRenderContext *ctx,
                               struct wl_shm *wl_shm,
                               int width,
                               int height,
                               const TypioPopupGeometry *geom,
                               const TypioCandidatePopupPalette *palette,
                               struct wl_buffer **out_buffer,
                               void **out_data);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_RENDER_API_H */
