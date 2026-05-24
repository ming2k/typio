/**
 * @file candidate_panel_layout.h
 * @brief Candidate panel geometry: LRU layout cache and immutable geometry snapshots.
 */

#ifndef TYPIO_WL_CANDIDATE_POPUP_LAYOUT_H
#define TYPIO_WL_CANDIDATE_POPUP_LAYOUT_H

#include "candidate_panel_theme.h"
#include "typio/input_context.h"
#include "typio/instance.h"
#include "typio/renderer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── Constants ──────────────────────────────────────────────────────── */

#define POPUP_LAYOUT_CACHE_CAP  128  /* LRU cache capacity (entries)     */
#define POPUP_MAX_ROWS          16   /* max candidates shown per page     */
#define POPUP_MIN_WIDTH         220  /* minimum popup width (logical px)  */
#define POPUP_PAD_X             10
#define POPUP_PAD_Y             6
#define POPUP_ROW_PAD_X         6    /* horizontal padding inside each row */
#define POPUP_ROW_PAD_Y         3    /* vertical padding inside each row   */
#define POPUP_ROW_GAP           0    /* gap between rows (vertical layout) */
#define POPUP_COL_GAP           2    /* gap between columns (horizontal)   */
#define POPUP_SECTION_GAP       6
#define POPUP_LABEL_GAP         5    /* gap between index label and text   */
#define POPUP_DEFAULT_FONT_SIZE 16

/* ── Configuration ──────────────────────────────────────────────────── */

typedef enum {
    POPUP_LAYOUT_VERTICAL = 0,
    POPUP_LAYOUT_HORIZONTAL,
} PopupLayoutMode;

typedef struct {
    bool   has_bg;         double bg_r, bg_g, bg_b, bg_a;
    bool   has_border;     double border_r, border_g, border_b, border_a;
    bool   has_text;       double text_r, text_g, text_b;
    bool   has_muted;      double muted_r, muted_g, muted_b;
    bool   has_preedit;    double preedit_r, preedit_g, preedit_b;
    bool   has_selection;  double selection_r, selection_g, selection_b, selection_a;
    bool   has_sel_text;   double sel_text_r, sel_text_g, sel_text_b;
} PopupThemeVariant;

typedef struct {
    TypioCandidatePanelThemeMode theme_mode;
    PopupLayoutMode              layout_mode;
    int                          font_size;
    bool                         mode_indicator;
    char                         font_desc[128];
    char                         label_font_desc[128];
    char                         aux_font_desc[128];
    char                         font_family[80];
    PopupThemeVariant            light_custom;
    PopupThemeVariant            dark_custom;
} PopupConfig;

/* ── Per-row geometry ───────────────────────────────────────────────── */

typedef struct {
    /* All four layouts borrowed from PopupRenderCtx; do NOT free here.
     * _sel variants carry the selection-text colour and are used when
     * this row is the highlighted candidate. */
    TypioTextLayout *label_layout;
    TypioTextLayout *layout;
    TypioTextLayout *label_layout_sel;
    TypioTextLayout *layout_sel;

    int label_w, label_h;
    int text_w,  text_h;

    int   x, y;   /* pixel-aligned row bounds (for damage regions / fills) */
    int   w, h;

    float label_x, label_y;   /* subpixel-accurate paint origins */
    float text_x,  text_y;

    float label_ink_y_offset;
    float text_ink_y_offset;
} PopupRow;

/* ── Geometry snapshot ──────────────────────────────────────────────── */

typedef struct {
    PopupRow rows[POPUP_MAX_ROWS];
    size_t   row_count;

    /* Preedit zone — shows either an IME preedit string or a transient
     * status message (e.g. "[Recording...]"); both use the same palette
     * colour and the same layout slot. May be NULL. */
    TypioTextLayout *preedit_layout;
    float        pre_x, pre_y;   /* subpixel-accurate */
    int          pre_w, pre_h;

    /* Mode label — owned by this geometry (may be NULL) */
    TypioTextLayout *mode_layout;
    float        mode_x, mode_y; /* subpixel-accurate */
    int          mode_w, mode_h;
    int          mode_divider_y; /* -1 if no divider */

    int popup_w, popup_h;   /* logical-pixel dimensions */
    /* Logical-to-physical pixel ratio. 1.0f on unscaled, integer for
     * legacy integer scale (e.g. 2.0f), arbitrary for fractional scale
     * (e.g. 1.25f). Drives font scaling, ink metrics, and the swapchain
     * physical extent. */
    float scale;

    uint64_t    content_sig;
    uint64_t    palette_sig;
    char        preedit_text[256];
    char        mode_label[128];
    PopupConfig config;

    TypioCandidatePanelPalette resolved_palette;
    const TypioCandidatePanelPalette *palette;
} PopupGeometry;

/* ── LRU layout cache ───────────────────────────────────────────────── */

typedef struct {
    uint64_t     key;
    uint32_t     label_color_packed;    /* ARGB8888, part of cache identity */
    uint32_t     text_color_packed;
    char         label[64];
    char         text[512];
    char         label_font_desc[96];
    char         font_desc[96];
    TypioTextLayout *label_layout;
    TypioTextLayout *layout;
    float        label_pixel_w;
    float        label_pixel_h;
    float        label_pixel_baseline;  /* alphabetic baseline from top */
    float        pixel_w;
    float        pixel_h;
    float        pixel_baseline;        /* alphabetic baseline from top */
    uint32_t     lru_tick;
} PopupLayoutEntry;

/* ── Persistent text engine + LRU cache ───────────────────────────── */

/* When the LRU evicts an entry whose layout/label_layout may still be
 * referenced by a geometry parked in the popup's retire ring (or by an
 * in-flight GPU frame), the popup MUST take ownership of the freeing —
 * an immediate free here is a use-after-free. PopupRenderCtx defers to
 * this callback when set; if it is NULL the layout is freed eagerly. */
typedef struct PopupRenderCtx PopupRenderCtx;
typedef void (*PopupLayoutEvictFn)(void *user, TypioTextLayout *layout);

struct PopupRenderCtx {
    TypioTextEngine  *engine;
    PopupLayoutEntry  entries[POPUP_LAYOUT_CACHE_CAP];
    uint32_t          tick;
    PopupLayoutEvictFn evict_cb;   /* NULL ⇒ free eagerly */
    void              *evict_user;
};

/* ── Functions ──────────────────────────────────────────────────────── */

void popup_render_ctx_init(PopupRenderCtx *pc);
void popup_render_ctx_set_evict(PopupRenderCtx *pc,
                                PopupLayoutEvictFn cb, void *user);
void popup_render_ctx_free(PopupRenderCtx *pc);
void popup_render_ctx_invalidate(PopupRenderCtx *pc);

PopupGeometry *popup_geometry_compute(PopupRenderCtx *pc,
                                      const TypioCandidateList *candidates,
                                      const char *preedit_text,
                                      const char *mode_label,
                                      const PopupConfig *config,
                                      const TypioCandidatePanelPalette *palette,
                                      float scale);

PopupGeometry *popup_geometry_update_aux(PopupRenderCtx *pc,
                                         const PopupGeometry *base,
                                         const char *preedit_text,
                                         const char *mode_label);

void popup_geometry_free(PopupGeometry *g);

void popup_config_load(PopupConfig *cfg, TypioInstance *instance);

void popup_config_build_palette(const PopupConfig *cfg,
                                 TypioCandidatePanelThemeCache *cache,
                                 TypioCandidatePanelPalette *out_palette);

#endif /* TYPIO_WL_CANDIDATE_POPUP_LAYOUT_H */
