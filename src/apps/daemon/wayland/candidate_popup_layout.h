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
#define POPUP_PADDING           8
#define POPUP_ROW_PAD_X         4
#define POPUP_ROW_PAD_Y         2
#define POPUP_ROW_GAP           4
#define POPUP_COL_GAP           10
#define POPUP_SECTION_GAP       6
#define POPUP_DEFAULT_FONT_SIZE 11

/* ── Configuration ──────────────────────────────────────────────────── */

typedef enum {
    POPUP_LAYOUT_VERTICAL = 0,
    POPUP_LAYOUT_HORIZONTAL,
} PopupLayoutMode;

typedef struct {
    TypioCandidatePopupThemeMode theme_mode;
    PopupLayoutMode              layout_mode;
    int                          font_size;
    bool                         mode_indicator;
    char                         font_desc[64];     /* candidates */
    char                         aux_font_desc[64]; /* preedit + mode label */
} PopupConfig;

/* ── Per-row geometry ───────────────────────────────────────────────── */

typedef struct {
    PangoLayout *layout;   /* borrowed from PopupPangoCtx; do NOT free here */
    int text_w, text_h;    /* measured text size (logical px)               */
    int x, y;              /* row background rect origin                    */
    int w, h;              /* row background rect size                      */
    int text_x, text_y;   /* text draw origin within the row               */
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
    char        preedit_text[256];
    char        mode_label[128];
    PopupConfig config;
    const TypioCandidatePopupPalette *palette;
} PopupGeometry;

/* ── LRU layout cache ───────────────────────────────────────────────── */

typedef struct {
    uint64_t     key;          /* FNV-1a hash(formatted_text + font_desc) */
    char         text[512];    /* formatted candidate text                 */
    char         font_desc[64];
    PangoLayout *layout;       /* owned by this entry                      */
    int          pixel_w;
    int          pixel_h;
    uint32_t     lru_tick;
} PopupLayoutEntry;

/* ── Persistent Pango context + LRU cache ───────────────────────────── */

typedef struct {
    PangoContext         *ctx;
    PangoFontDescription *font;      /* main font, matched to config.font_desc     */
    PangoFontDescription *aux_font;  /* aux font, matched to config.aux_font_desc  */
    char                  font_desc[64];
    char                  aux_font_desc[64];
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

#endif /* TYPIO_WL_CANDIDATE_POPUP_LAYOUT_H */
