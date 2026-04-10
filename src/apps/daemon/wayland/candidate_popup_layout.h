/**
 * @file candidate_popup_layout.h
 * @brief Candidate popup geometry: LRU layout cache and immutable geometry snapshots.
 *
 * Design:
 *
 *   PopupPangoCtx  — one persistent PangoContext per popup, plus an LRU
 *                    cache of PangoLayouts keyed by (formatted_text, font_desc).
 *                    Layouts are created without a scratch Cairo surface;
 *                    pango_cairo_update_layout() is called during painting.
 *
 *   PopupGeometry  — an immutable snapshot of the computed layout for one
 *                    candidate page.  `selected` is NOT part of the geometry
 *                    because changing the selection does not affect positions.
 *                    Candidate row layouts are *borrowed* from PopupPangoCtx.
 *                    Preedit and mode-label layouts are *owned* by the geometry.
 */

#ifndef TYPIO_WL_CANDIDATE_POPUP_LAYOUT_H
#define TYPIO_WL_CANDIDATE_POPUP_LAYOUT_H

#include "candidate_popup_theme.h"
#include "typio/input_context.h"
#include "typio/instance.h"

#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── Constants ──────────────────────────────────────────────────────── */

#define POPUP_LAYOUT_CACHE_CAP  64   /* LRU cache capacity (entries)     */
#define POPUP_MAX_ROWS          16   /* max candidates shown per page     */
#define POPUP_MIN_WIDTH         220  /* minimum popup width (logical px)  */
#define POPUP_PADDING           6
#define POPUP_ROW_PAD_X         3    /* horizontal padding inside each row */
#define POPUP_ROW_PAD_Y         4    /* vertical padding inside each row   */
#define POPUP_ROW_GAP           0    /* gap between rows (vertical layout) */
#define POPUP_COL_GAP           6    /* gap between columns (horizontal)   */
#define POPUP_SECTION_GAP       6
#define POPUP_LABEL_GAP         5    /* gap between index label and text   */
#define POPUP_DEFAULT_FONT_SIZE 11

/* ── Configuration ──────────────────────────────────────────────────── */

typedef enum {
    POPUP_LAYOUT_VERTICAL = 0,
    POPUP_LAYOUT_HORIZONTAL,
} PopupLayoutMode;

/**
 * Per-channel color customisation for one theme variant (light or dark).
 * Loaded from [display.colors.light] or [display.colors.dark] in typio.toml.
 * Each "has_*" flag is set when the user supplied that color explicitly;
 * unset channels fall back to the built-in palette for that variant.
 */
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
    TypioCandidatePopupThemeMode theme_mode;
    PopupLayoutMode              layout_mode;
    int                          font_size;
    bool                         mode_indicator;
    char                         font_desc[96];       /* candidates */
    char                         label_font_desc[96]; /* index labels (auto-scaled smaller) */
    char                         aux_font_desc[96];   /* preedit + mode label */
    char                         font_family[80];     /* font family name      */
    PopupThemeVariant            light_custom;  /* user overrides for light mode */
    PopupThemeVariant            dark_custom;   /* user overrides for dark mode  */
} PopupConfig;

/* ── Per-row geometry ───────────────────────────────────────────────── */

typedef struct {
    /* Both layouts borrowed from PopupPangoCtx; do NOT free here */
    PangoLayout *label_layout; /* index label (e.g. "1", "a")              */
    PangoLayout *layout;       /* candidate text (e.g. "的  comment")      */

    int label_w, label_h;  /* label width + visible ink height (px)         */
    int text_w,  text_h;   /* candidate width + visible ink height (px)     */

    int x, y;              /* row background rect origin (logical px)       */
    int w, h;              /* row background rect size (logical px)         */

    int label_x, label_y;  /* label draw origin                             */
    int text_x,  text_y;   /* candidate text draw origin                    */

    int label_ink_y_offset; /* correction: -(label_ink_rect.y)              */
    int text_ink_y_offset;  /* correction: -(text_ink_rect.y)               */
} PopupRow;

/* ── Geometry snapshot ──────────────────────────────────────────────── */

typedef struct {
    PopupRow rows[POPUP_MAX_ROWS];
    size_t   row_count;

    /* Preedit — owned by this geometry (may be NULL) */
    PangoLayout *preedit_layout;
    int          pre_x, pre_y;
    int          pre_w, pre_h;

    /* Mode label — owned by this geometry (may be NULL) */
    PangoLayout *mode_layout;
    int          mode_x, mode_y;
    int          mode_w, mode_h;
    int          mode_divider_y; /* -1 if no divider */

    int popup_w, popup_h;  /* logical pixels */
    int scale;

    /* Saved for delta classification on next update */
    uint64_t    content_sig;
    uint64_t    palette_sig;   /* FNV-1a hash of resolved_palette for change detection */
    char        preedit_text[256];
    char        mode_label[128];
    PopupConfig config;

    /* Owned copy of the effective palette (built-in preset + user overrides) */
    TypioCandidatePopupPalette resolved_palette;
    const TypioCandidatePopupPalette *palette;  /* always == &resolved_palette */
} PopupGeometry;

/* ── LRU layout cache ───────────────────────────────────────────────── */

typedef struct {
    uint64_t     key;              /* FNV-1a hash(label + text + label_font_desc + font_desc) */
    char         label[64];        /* index label text (e.g. "1", "a")       */
    char         text[512];        /* candidate text (e.g. "的  comment")    */
    char         label_font_desc[96];
    char         font_desc[96];
    PangoLayout *label_layout;     /* owned: label portion                   */
    PangoLayout *layout;           /* owned: candidate text portion          */
    int          label_pixel_w;    /* label logical width                    */
    int          label_pixel_h;    /* label logical height                   */
    int          label_ink_y;      /* label ink rect Y offset               */
    int          label_ink_h;      /* label ink rect height                 */
    int          pixel_w;          /* candidate text logical width           */
    int          pixel_h;          /* candidate text logical height (unused for sizing) */
    int          ink_y;            /* ink rect Y offset from logical top     */
    int          ink_h;            /* ink rect height (used for row sizing)  */
    uint32_t     lru_tick;
} PopupLayoutEntry;

/* ── Persistent Pango context + LRU cache ───────────────────────────── */

typedef struct {
    PangoContext         *ctx;
    PangoFontDescription *font;        /* main font, matched to config.font_desc       */
    PangoFontDescription *label_font;  /* label font, matched to config.label_font_desc */
    PangoFontDescription *aux_font;    /* aux font, matched to config.aux_font_desc    */
    char                  font_desc[96];
    char                  label_font_desc[96];
    char                  aux_font_desc[96];
    PopupLayoutEntry      entries[POPUP_LAYOUT_CACHE_CAP];
    uint32_t              tick;
} PopupPangoCtx;

/* ── Functions ──────────────────────────────────────────────────────── */

/* Initialise / tear down the Pango context. */
void popup_pango_ctx_init(PopupPangoCtx *pc);
void popup_pango_ctx_free(PopupPangoCtx *pc);

/* Invalidate font descriptions and all cached layouts (call on font/scale change). */
void popup_pango_ctx_invalidate(PopupPangoCtx *pc);

/**
 * Compute a full geometry snapshot from scratch.
 * Candidate layouts are looked up or inserted into the LRU cache.
 * Preedit and mode-label layouts are created fresh (owned by the returned geometry).
 * Returns a heap-allocated PopupGeometry, or NULL on allocation failure.
 */
PopupGeometry *popup_geometry_compute(PopupPangoCtx *pc,
                                      const TypioCandidateList *candidates,
                                      const char *preedit_text,
                                      const char *mode_label,
                                      const PopupConfig *config,
                                      const TypioCandidatePopupPalette *palette,
                                      int scale);

/**
 * Rebuild only the aux section (preedit + mode label) while reusing the row
 * layouts and positions from @base.  Returns a new geometry if the new
 * preedit/mode label fits within the same popup dimensions; returns NULL if
 * the dimensions would change (caller should fall back to popup_geometry_compute).
 */
PopupGeometry *popup_geometry_update_aux(PopupPangoCtx *pc,
                                         const PopupGeometry *base,
                                         const char *preedit_text,
                                         const char *mode_label);

/** Free a geometry snapshot (owned layouts only; borrowed row layouts are left alone). */
void popup_geometry_free(PopupGeometry *g);

/** Load PopupConfig from the global TypioInstance config. */
void popup_config_load(PopupConfig *cfg, TypioInstance *instance);

/**
 * Build the effective palette for @cfg by resolving the theme preset and
 * applying any user-defined color overrides from [display.colors].
 * The result is written into @out_palette.
 */
void popup_config_build_palette(const PopupConfig *cfg,
                                 TypioCandidatePopupThemeCache *cache,
                                 TypioCandidatePopupPalette *out_palette);

#endif /* TYPIO_WL_CANDIDATE_POPUP_LAYOUT_H */
