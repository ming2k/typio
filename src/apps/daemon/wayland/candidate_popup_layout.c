/**
 * @file candidate_popup_layout.c
 * @brief Candidate popup geometry: PangoContext-based LRU cache and geometry computation.
 *
 * Key design choices:
 *
 *   - Text measurement uses a persistent PangoContext (pango_font_map_create_context)
 *     rather than creating a 1×1 scratch Cairo surface on every page change.
 *
 *   - Candidate PangoLayouts are cached in a 64-entry LRU keyed by
 *     FNV-1a hash(formatted_text + font_desc).  On a typical page change,
 *     most layouts are already cached because the user has seen those
 *     candidates before, or the same characters appear across pages.
 *
 *   - `selected` is intentionally absent from PopupGeometry: changing the
 *     selection never requires re-measuring text or recomputing positions.
 */

#include "candidate_popup_layout.h"
#include "typio/config.h"

#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── FNV-1a hash ────────────────────────────────────────────────────── */

static uint64_t fnv1a(uint64_t h, const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        h ^= *p++;
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t layout_cache_key(const char *label, const char *text,
                                  const char *label_font_desc,
                                  const char *font_desc) {
    uint64_t h = 14695981039346656037ULL;
    h = fnv1a(h, label);
    h ^= 0x01ULL; h *= 1099511628211ULL;
    h = fnv1a(h, text);
    h ^= 0xffULL; h *= 1099511628211ULL;
    h = fnv1a(h, label_font_desc);
    h ^= 0x55ULL; h *= 1099511628211ULL;
    h = fnv1a(h, font_desc);
    return h;
}

/* ── Candidate text formatting ──────────────────────────────────────── */

/**
 * Split a candidate into separate label and text parts for independent rendering.
 * The label (e.g. "1", "a") will be drawn in muted color; the text in primary.
 */
static void format_candidate_parts(const TypioCandidate *c, size_t idx,
                                    char *label_buf, size_t label_size,
                                    char *text_buf,  size_t text_size) {
    char fallback_label[32];
    const char *label;

    if (!c || !c->text) {
        if (label_buf && label_size) label_buf[0] = '\0';
        if (text_buf  && text_size)  text_buf[0]  = '\0';
        return;
    }

    if (c->label && c->label[0]) {
        label = c->label;
    } else {
        snprintf(fallback_label, sizeof(fallback_label), "%zu", idx + 1);
        label = fallback_label;
    }

    snprintf(label_buf, label_size, "%s", label);

    if (c->comment && c->comment[0]) {
        snprintf(text_buf, text_size, "%s  %s", c->text, c->comment);
    } else {
        snprintf(text_buf, text_size, "%s", c->text);
    }
}

/* ── PangoContext management ────────────────────────────────────────── */

static void pango_ctx_rebuild_fonts(PopupPangoCtx *pc) {
    if (pc->font) {
        pango_font_description_free(pc->font);
        pc->font = nullptr;
    }
    if (pc->label_font) {
        pango_font_description_free(pc->label_font);
        pc->label_font = nullptr;
    }
    if (pc->aux_font) {
        pango_font_description_free(pc->aux_font);
        pc->aux_font = nullptr;
    }

    if (pc->font_desc[0]) {
        pc->font = pango_font_description_from_string(pc->font_desc);
    }
    if (pc->label_font_desc[0]) {
        pc->label_font = pango_font_description_from_string(pc->label_font_desc);
    }
    if (pc->aux_font_desc[0]) {
        pc->aux_font = pango_font_description_from_string(pc->aux_font_desc);
    }
}

void popup_pango_ctx_init(PopupPangoCtx *pc) {
    PangoFontMap *font_map;

    if (!pc) return;

    memset(pc, 0, sizeof(*pc));

    font_map = pango_cairo_font_map_get_default();
    pc->ctx  = pango_font_map_create_context(font_map);

    if (pc->ctx) {
        /* Set font options for consistent rendering */
        cairo_font_options_t *opts = cairo_font_options_create();
        cairo_font_options_set_antialias(opts, CAIRO_ANTIALIAS_SUBPIXEL);
        cairo_font_options_set_hint_style(opts, CAIRO_HINT_STYLE_SLIGHT);
        pango_cairo_context_set_font_options(pc->ctx, opts);
        cairo_font_options_destroy(opts);
    }
}

void popup_pango_ctx_free(PopupPangoCtx *pc) {
    size_t i;

    if (!pc) return;

    for (i = 0; i < POPUP_LAYOUT_CACHE_CAP; ++i) {
        if (pc->entries[i].layout) {
            g_object_unref(pc->entries[i].layout);
            pc->entries[i].layout = nullptr;
        }
        if (pc->entries[i].label_layout) {
            g_object_unref(pc->entries[i].label_layout);
            pc->entries[i].label_layout = nullptr;
        }
    }

    if (pc->font) {
        pango_font_description_free(pc->font);
        pc->font = nullptr;
    }
    if (pc->label_font) {
        pango_font_description_free(pc->label_font);
        pc->label_font = nullptr;
    }
    if (pc->aux_font) {
        pango_font_description_free(pc->aux_font);
        pc->aux_font = nullptr;
    }
    if (pc->ctx) {
        g_object_unref(pc->ctx);
        pc->ctx = nullptr;
    }

    memset(pc, 0, sizeof(*pc));
}

void popup_pango_ctx_invalidate(PopupPangoCtx *pc) {
    size_t i;

    if (!pc) return;

    /* Clear all cached layouts — font metrics may have changed */
    for (i = 0; i < POPUP_LAYOUT_CACHE_CAP; ++i) {
        if (pc->entries[i].layout) {
            g_object_unref(pc->entries[i].layout);
        }
        if (pc->entries[i].label_layout) {
            g_object_unref(pc->entries[i].label_layout);
        }
        memset(&pc->entries[i], 0, sizeof(pc->entries[i]));
    }
    pc->tick = 0;

    pango_ctx_rebuild_fonts(pc);
}

/* ── Font cache helpers ─────────────────────────────────────────────── */

static bool pango_ctx_ensure_font(PopupPangoCtx *pc,
                                   const char *font_desc,
                                   const char *label_font_desc,
                                   const char *aux_font_desc) {
    bool changed = false;

    if (!pc || !pc->ctx) return false;

    if (strcmp(pc->font_desc, font_desc ? font_desc : "") != 0) {
        snprintf(pc->font_desc, sizeof(pc->font_desc),
                 "%s", font_desc ? font_desc : "");
        changed = true;
    }
    if (strcmp(pc->label_font_desc, label_font_desc ? label_font_desc : "") != 0) {
        snprintf(pc->label_font_desc, sizeof(pc->label_font_desc),
                 "%s", label_font_desc ? label_font_desc : "");
        changed = true;
    }
    if (strcmp(pc->aux_font_desc, aux_font_desc ? aux_font_desc : "") != 0) {
        snprintf(pc->aux_font_desc, sizeof(pc->aux_font_desc),
                 "%s", aux_font_desc ? aux_font_desc : "");
        changed = true;
    }

    if (changed) {
        /* Fonts changed: all cached layouts are stale */
        popup_pango_ctx_invalidate(pc);
    }

    if (!pc->font && pc->font_desc[0]) {
        pc->font = pango_font_description_from_string(pc->font_desc);
    }
    if (!pc->label_font && pc->label_font_desc[0]) {
        pc->label_font = pango_font_description_from_string(pc->label_font_desc);
    }
    if (!pc->aux_font && pc->aux_font_desc[0]) {
        pc->aux_font = pango_font_description_from_string(pc->aux_font_desc);
    }

    return true;
}

/* ── LRU cache lookup / insert ──────────────────────────────────────── */

/**
 * Find or create a cached entry for (label, text, font_desc).
 *
 * On hit: bump lru_tick and return the entry.
 * On miss: evict the LRU slot, create two PangoLayouts (label + text),
 *          measure ink extents for vertical-alignment correction, return entry.
 *
 * Returns NULL only if PangoContext is unavailable.
 */
static PopupLayoutEntry *lru_get_or_create(PopupPangoCtx *pc,
                                            const char *label,
                                            const char *text,
                                            PangoFontDescription *label_font,
                                            const char *label_font_desc,
                                            PangoFontDescription *font,
                                            const char *font_desc) {
    uint64_t key;
    size_t   i;
    size_t   lru_idx  = 0;
    uint32_t lru_tick_min = UINT32_MAX;
    PangoLayout     *text_layout;
    PangoLayout     *lbl_layout;
    PangoRectangle   ink;
    PangoRectangle   label_ink;
    const char      *lfd = label_font_desc ? label_font_desc : "";
    const char      *fd  = font_desc ? font_desc : "";

    if (!pc || !pc->ctx || !label || !text || !label_font || !font) return nullptr;

    key = layout_cache_key(label, text, lfd, fd);
    pc->tick++;

    /* Linear scan: cache hit or find LRU victim */
    for (i = 0; i < POPUP_LAYOUT_CACHE_CAP; ++i) {
        PopupLayoutEntry *e = &pc->entries[i];

        if (e->layout && e->key == key &&
            strcmp(e->label, label) == 0 &&
            strcmp(e->text,  text)  == 0 &&
            strcmp(e->label_font_desc, lfd) == 0 &&
            strcmp(e->font_desc, fd) == 0) {
            e->lru_tick = pc->tick;
            return e;
        }

        if (!e->layout || e->lru_tick < lru_tick_min) {
            lru_tick_min = e->layout ? e->lru_tick : 0;
            lru_idx      = i;
        }
    }

    /* Cache miss: evict LRU slot */
    PopupLayoutEntry *victim = &pc->entries[lru_idx];
    if (victim->layout) {
        g_object_unref(victim->layout);
        victim->layout = nullptr;
    }
    if (victim->label_layout) {
        g_object_unref(victim->label_layout);
        victim->label_layout = nullptr;
    }

    /* Create label layout — uses the smaller label_font */
    lbl_layout = pango_layout_new(pc->ctx);
    if (!lbl_layout) return nullptr;
    pango_layout_set_font_description(lbl_layout, label_font);
    pango_layout_set_text(lbl_layout, label, -1);
    pango_layout_get_pixel_size(lbl_layout, &victim->label_pixel_w,
                                             &victim->label_pixel_h);
    pango_layout_get_pixel_extents(lbl_layout, &label_ink, nullptr);
    victim->label_ink_y = label_ink.y;
    victim->label_ink_h = label_ink.height > 0 ? label_ink.height
                                               : victim->label_pixel_h;

    /* Create candidate-text layout and measure ink extents for y-correction */
    text_layout = pango_layout_new(pc->ctx);
    if (!text_layout) {
        g_object_unref(lbl_layout);
        return nullptr;
    }
    pango_layout_set_font_description(text_layout, font);
    pango_layout_set_text(text_layout, text, -1);
    pango_layout_get_pixel_size(text_layout, &victim->pixel_w, &victim->pixel_h);
    pango_layout_get_pixel_extents(text_layout, &ink, nullptr);
    victim->ink_y = ink.y;
    victim->ink_h = ink.height > 0 ? ink.height : victim->pixel_h;

    victim->key          = key;
    victim->layout       = text_layout;
    victim->label_layout = lbl_layout;
    victim->lru_tick     = pc->tick;
    snprintf(victim->label,            sizeof(victim->label),            "%s", label);
    snprintf(victim->text,             sizeof(victim->text),             "%s", text);
    snprintf(victim->label_font_desc,  sizeof(victim->label_font_desc),  "%s", lfd);
    snprintf(victim->font_desc,        sizeof(victim->font_desc),        "%s", fd);

    return victim;
}

/* ── Standalone layout creation (preedit / mode label) ──────────────── */

static PangoLayout *make_layout(PopupPangoCtx *pc,
                                 PangoFontDescription *font,
                                 const char *text,
                                 int *out_w, int *out_h) {
    PangoLayout *l;

    if (!pc || !pc->ctx || !font || !text) return nullptr;

    l = pango_layout_new(pc->ctx);
    if (!l) return nullptr;

    pango_layout_set_font_description(l, font);
    pango_layout_set_text(l, text, -1);
    pango_layout_get_pixel_size(l, out_w, out_h);
    return l;
}

/* ── Geometry helpers ───────────────────────────────────────────────── */

static void compute_positions_vertical(PopupGeometry *g, int pre_h_used) {
    int content_w = g->pre_w;
    int content_h = pre_h_used ? g->pre_h : 0;
    int max_label_w = 0;
    size_t i;

    /* Find the widest label so all text aligns in a column */
    for (i = 0; i < g->row_count; ++i) {
        if (g->rows[i].label_w > max_label_w) max_label_w = g->rows[i].label_w;
    }

    /* Measure content area */
    for (i = 0; i < g->row_count; ++i) {
        /* Row width uses max label column so all rows are the same width */
        int rw = max_label_w + POPUP_LABEL_GAP + g->rows[i].text_w
                 + POPUP_ROW_PAD_X * 2;
        if (rw > content_w) content_w = rw;
        content_h += g->rows[i].h;
        if (i + 1 < g->row_count) content_h += POPUP_ROW_GAP;
    }

    if (pre_h_used && g->row_count > 0) content_h += POPUP_SECTION_GAP;

    /* Mode label adds to height here */
    if (g->mode_layout && g->mode_h > 0) {
        content_h += POPUP_SECTION_GAP + g->mode_h;
        if (g->mode_w > content_w) content_w = g->mode_w;
    }

    g->popup_w = content_w + POPUP_PADDING * 2;
    if (g->popup_w < POPUP_MIN_WIDTH) g->popup_w = POPUP_MIN_WIDTH;
    g->popup_h = content_h + POPUP_PADDING * 2;

    /* Row positions */
    int y = POPUP_PADDING;
    if (pre_h_used) y += g->pre_h + POPUP_SECTION_GAP;

    for (i = 0; i < g->row_count; ++i) {
        int row_content_h = g->rows[i].h - POPUP_ROW_PAD_Y * 2;
        int label_top = y + POPUP_ROW_PAD_Y + (row_content_h - g->rows[i].label_h) / 2;
        int text_top  = y + POPUP_ROW_PAD_Y + (row_content_h - g->rows[i].text_h) / 2;
        g->rows[i].x       = POPUP_PADDING;
        g->rows[i].y       = y;
        g->rows[i].w       = g->popup_w - POPUP_PADDING * 2;
        /* Label left-aligned; all candidate texts share the same column */
        g->rows[i].label_x = g->rows[i].x + POPUP_ROW_PAD_X;
        g->rows[i].label_y = label_top + g->rows[i].label_ink_y_offset;
        g->rows[i].text_x  = g->rows[i].x + POPUP_ROW_PAD_X
                              + max_label_w + POPUP_LABEL_GAP;
        g->rows[i].text_y  = text_top + g->rows[i].text_ink_y_offset;
        y += g->rows[i].h;
        if (i + 1 < g->row_count) y += POPUP_ROW_GAP;
    }

    /* Preedit position */
    g->pre_x = POPUP_PADDING;
    g->pre_y = POPUP_PADDING;

    /* Mode label position */
    if (g->mode_layout && g->mode_h > 0) {
        g->mode_x = g->popup_w - POPUP_PADDING - g->mode_w;
        g->mode_divider_y = g->popup_h - POPUP_PADDING -
                            g->mode_h - POPUP_ROW_PAD_Y;
        g->mode_y        = g->popup_h - POPUP_PADDING - g->mode_h;
    } else {
        g->mode_divider_y = -1;
    }
}

static void compute_positions_horizontal(PopupGeometry *g, int pre_h_used) {
    int content_w = g->pre_w;
    int content_h = pre_h_used ? g->pre_h : 0;
    int row_w     = 0;
    int row_h     = 0;
    size_t i;

    /* Measure content area */
    for (i = 0; i < g->row_count; ++i) {
        if (row_w > 0) row_w += POPUP_COL_GAP;
        row_w += g->rows[i].w;
        if (g->rows[i].h > row_h) row_h = g->rows[i].h;
    }
    content_h += row_h;
    int total_row = row_w;
    if (g->mode_layout && g->mode_w > 0) {
        total_row += POPUP_COL_GAP + g->mode_w;
    }
    content_w = total_row > g->pre_w ? total_row : g->pre_w;

    if (pre_h_used && g->row_count > 0) content_h += POPUP_SECTION_GAP;

    g->popup_w = content_w + POPUP_PADDING * 2;
    if (g->popup_w < POPUP_MIN_WIDTH) g->popup_w = POPUP_MIN_WIDTH;
    g->popup_h = content_h + POPUP_PADDING * 2;

    /* Row positions */
    int y = POPUP_PADDING;
    if (pre_h_used) y += g->pre_h + POPUP_SECTION_GAP;

    int x = POPUP_PADDING;
    for (i = 0; i < g->row_count; ++i) {
        int row_content_h = g->rows[i].h - POPUP_ROW_PAD_Y * 2;
        int label_top = y + POPUP_ROW_PAD_Y + (row_content_h - g->rows[i].label_h) / 2;
        int text_top  = y + POPUP_ROW_PAD_Y + (row_content_h - g->rows[i].text_h) / 2;
        g->rows[i].x       = x;
        g->rows[i].y       = y;
        g->rows[i].label_x = x + POPUP_ROW_PAD_X;
        g->rows[i].label_y = label_top + g->rows[i].label_ink_y_offset;
        g->rows[i].text_x  = x + POPUP_ROW_PAD_X
                              + g->rows[i].label_w + POPUP_LABEL_GAP;
        g->rows[i].text_y  = text_top + g->rows[i].text_ink_y_offset;
        x += g->rows[i].w + POPUP_COL_GAP;
    }

    /* Preedit position */
    g->pre_x = POPUP_PADDING;
    g->pre_y = POPUP_PADDING;

    /* Mode label position */
    if (g->mode_layout && g->mode_h > 0) {
        g->mode_x = g->popup_w - POPUP_PADDING - g->mode_w;
        g->mode_y        = y + POPUP_ROW_PAD_Y;
        g->mode_divider_y = -1;
    } else {
        g->mode_divider_y = -1;
    }
}

/* Compute the popup width and height, and assign row positions. */
static void compute_positions(PopupGeometry *g,
                               const PopupConfig *cfg,
                               int pre_h_used) {
    if (cfg->layout_mode == POPUP_LAYOUT_VERTICAL) {
        compute_positions_vertical(g, pre_h_used);
    } else {
        compute_positions_horizontal(g, pre_h_used);
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

PopupGeometry *popup_geometry_compute(PopupPangoCtx *pc,
                                      const TypioCandidateList *candidates,
                                      const char *preedit_text,
                                      const char *mode_label,
                                      const PopupConfig *cfg,
                                      const TypioCandidatePopupPalette *palette,
                                      int scale) {
    PopupGeometry *g;
    size_t i;

    if (!pc || !candidates || !cfg || !palette) return nullptr;
    if (candidates->count > POPUP_MAX_ROWS) return nullptr;

    /* Ensure fonts match current config */
    if (!pango_ctx_ensure_font(pc, cfg->font_desc, cfg->label_font_desc, cfg->aux_font_desc)) {
        return nullptr;
    }

    g = calloc(1, sizeof(*g));
    if (!g) return nullptr;

    g->row_count      = candidates->count;
    g->scale          = scale;
    g->content_sig    = candidates->content_signature;
    g->config         = *cfg;
    g->mode_divider_y = -1;

    /* Copy palette into owned field; pointer always points to embedded copy */
    g->resolved_palette = *palette;
    g->palette          = &g->resolved_palette;
    g->palette_sig      = typio_candidate_popup_palette_hash(palette);

    snprintf(g->preedit_text, sizeof(g->preedit_text),
             "%s", preedit_text ? preedit_text : "");
    snprintf(g->mode_label, sizeof(g->mode_label),
             "%s", mode_label ? mode_label : "");

    /* Measure each candidate row via the LRU cache */
    for (i = 0; i < candidates->count; ++i) {
        char label_buf[64], text_buf[512];
        PopupLayoutEntry *entry;

        format_candidate_parts(&candidates->candidates[i], i,
                                label_buf, sizeof(label_buf),
                                text_buf,  sizeof(text_buf));

        entry = lru_get_or_create(pc, label_buf, text_buf,
                                  pc->label_font, cfg->label_font_desc,
                                  pc->font, cfg->font_desc);
        if (!entry) {
            free(g);
            return nullptr;
        }

        g->rows[i].label_layout  = entry->label_layout;
        g->rows[i].layout        = entry->layout;
        g->rows[i].label_w       = entry->label_pixel_w;
        g->rows[i].label_h       = entry->label_ink_h;
        g->rows[i].text_w        = entry->pixel_w;
        g->rows[i].text_h        = entry->ink_h;
        g->rows[i].label_ink_y_offset = -entry->label_ink_y;
        g->rows[i].text_ink_y_offset  = -entry->ink_y;
        /* Row width: label + gap + text + horizontal padding */
        g->rows[i].w = entry->label_pixel_w + POPUP_LABEL_GAP
                       + entry->pixel_w + POPUP_ROW_PAD_X * 2;
        /* Temporarily store ink content height in h; normalised below. */
        g->rows[i].h = (entry->label_ink_h > entry->ink_h
                        ? entry->label_ink_h : entry->ink_h);
    }

    /* Normalise: all rows share the same height = max ink content height + padding.
     * Using per-glyph ink height (not Pango logical height) keeps rows compact;
     * taking the max across all rows ensures every row is identical in height. */
    {
        int max_content_h = 0;
        for (i = 0; i < g->row_count; ++i) {
            if (g->rows[i].h > max_content_h) max_content_h = g->rows[i].h;
        }
        for (i = 0; i < g->row_count; ++i) {
            g->rows[i].h = max_content_h + POPUP_ROW_PAD_Y * 2;
        }
    }

    /* Preedit layout (owned) */
    if (preedit_text && preedit_text[0] && pc->aux_font) {
        g->preedit_layout = make_layout(pc, pc->aux_font, preedit_text,
                                         &g->pre_w, &g->pre_h);
    }

    /* Mode label layout (owned) */
    if (cfg->mode_indicator && mode_label && mode_label[0] && pc->aux_font) {
        g->mode_layout = make_layout(pc, pc->aux_font, mode_label,
                                      &g->mode_w, &g->mode_h);
    }

    compute_positions(g, cfg, preedit_text && preedit_text[0] ? 1 : 0);

    return g;
}

PopupGeometry *popup_geometry_update_aux(PopupPangoCtx *pc,
                                          const PopupGeometry *base,
                                          const char *preedit_text,
                                          const char *mode_label) {
    PopupGeometry *g;
    int new_pre_w = 0, new_pre_h = 0;
    int new_mode_w = 0, new_mode_h = 0;
    PangoLayout *new_preedit_layout = nullptr;
    PangoLayout *new_mode_layout    = nullptr;

    if (!pc || !base) return nullptr;

    /* Measure new aux layouts */
    if (preedit_text && preedit_text[0] && pc->aux_font) {
        new_preedit_layout = make_layout(pc, pc->aux_font, preedit_text,
                                          &new_pre_w, &new_pre_h);
        if (!new_preedit_layout) return nullptr;
    }

    if (base->config.mode_indicator && mode_label && mode_label[0] && pc->aux_font) {
        new_mode_layout = make_layout(pc, pc->aux_font, mode_label,
                                       &new_mode_w, &new_mode_h);
        if (!new_mode_layout) {
            if (new_preedit_layout) g_object_unref(new_preedit_layout);
            return nullptr;
        }
    }

    /* If either aux dimension changed, dimensions would change → signal failure */
    bool pre_dims_same  = (new_pre_w == base->pre_w && new_pre_h == base->pre_h);
    bool mode_dims_same = (new_mode_w == base->mode_w && new_mode_h == base->mode_h);

    if (!pre_dims_same || !mode_dims_same) {
        if (new_preedit_layout) g_object_unref(new_preedit_layout);
        if (new_mode_layout)    g_object_unref(new_mode_layout);
        return nullptr;
    }

    /* Dimensions unchanged: build updated geometry by copying base */
    g = malloc(sizeof(*g));
    if (!g) {
        if (new_preedit_layout) g_object_unref(new_preedit_layout);
        if (new_mode_layout)    g_object_unref(new_mode_layout);
        return nullptr;
    }

    *g = *base;  /* shallow copy — rows[].layout pointers are still valid */

    /* resolved_palette was copied by value; fix self-referential pointer */
    g->palette = &g->resolved_palette;

    /* Replace owned aux layouts */
    g->preedit_layout = new_preedit_layout;
    g->pre_w          = new_pre_w;
    g->pre_h          = new_pre_h;
    g->mode_layout    = new_mode_layout;
    g->mode_w         = new_mode_w;
    g->mode_h         = new_mode_h;

    snprintf(g->preedit_text, sizeof(g->preedit_text),
             "%s", preedit_text ? preedit_text : "");
    snprintf(g->mode_label, sizeof(g->mode_label),
             "%s", mode_label ? mode_label : "");

    return g;
}

void popup_geometry_free(PopupGeometry *g) {
    if (!g) return;

    /* Row layouts are borrowed from the cache — do NOT free them */

    if (g->preedit_layout) {
        g_object_unref(g->preedit_layout);
    }
    if (g->mode_layout) {
        g_object_unref(g->mode_layout);
    }

    free(g);
}

void popup_config_load(PopupConfig *cfg, TypioInstance *instance) {
    TypioConfig *global_cfg;
    const char  *theme;
    const char  *layout;
    const char  *font_family;
    const char  *hex;
    int          font_size;

    if (!cfg) return;

    cfg->theme_mode     = TYPIO_CANDIDATE_POPUP_THEME_AUTO;
    cfg->layout_mode    = POPUP_LAYOUT_VERTICAL;
    cfg->font_size      = POPUP_DEFAULT_FONT_SIZE;
    cfg->mode_indicator = true;
    memset(&cfg->light_custom, 0, sizeof(cfg->light_custom));
    memset(&cfg->dark_custom,  0, sizeof(cfg->dark_custom));
    snprintf(cfg->font_family, sizeof(cfg->font_family), "Sans");

    global_cfg = instance ? typio_instance_get_config(instance) : nullptr;
    if (!global_cfg) goto build_descs;

    theme  = typio_config_get_string(global_cfg, "display.popup_theme",      nullptr);
    layout = typio_config_get_string(global_cfg, "display.candidate_layout", nullptr);
    font_size = typio_config_get_int(global_cfg, "display.font_size",
                                      POPUP_DEFAULT_FONT_SIZE);

    if (theme) {
        if      (strcmp(theme, "dark")  == 0) cfg->theme_mode = TYPIO_CANDIDATE_POPUP_THEME_DARK;
        else if (strcmp(theme, "light") == 0) cfg->theme_mode = TYPIO_CANDIDATE_POPUP_THEME_LIGHT;
        /* unknown value → keep AUTO */
    }

    if (layout && strcmp(layout, "horizontal") == 0) {
        cfg->layout_mode = POPUP_LAYOUT_HORIZONTAL;
    }

    if (font_size < 6)  font_size = 6;
    if (font_size > 72) font_size = 72;
    cfg->font_size = font_size;

    cfg->mode_indicator = typio_config_get_bool(global_cfg,
                                                 "display.popup_mode_indicator",
                                                 true);

    /* User-defined font family */
    font_family = typio_config_get_string(global_cfg, "display.font_family", nullptr);
    if (font_family && font_family[0]) {
        snprintf(cfg->font_family, sizeof(cfg->font_family), "%s", font_family);
    }

    /* Per-channel color customisation from [display.colors.light] and [display.colors.dark].
     * Custom values override the corresponding built-in palette when that variant is active. */

#define LOAD_VARIANT(section, ov) \
    hex = typio_config_get_string(global_cfg, section ".background",     nullptr); \
    if (hex) { (ov)->has_bg      = typio_parse_hex_color(hex, &(ov)->bg_r,      &(ov)->bg_g,      &(ov)->bg_b,      &(ov)->bg_a);      } \
    hex = typio_config_get_string(global_cfg, section ".border",         nullptr); \
    if (hex) { (ov)->has_border  = typio_parse_hex_color(hex, &(ov)->border_r,  &(ov)->border_g,  &(ov)->border_b,  &(ov)->border_a);  } \
    hex = typio_config_get_string(global_cfg, section ".text",           nullptr); \
    if (hex) { double _a = 1.0; (ov)->has_text = typio_parse_hex_color(hex, &(ov)->text_r, &(ov)->text_g, &(ov)->text_b, &_a); } \
    hex = typio_config_get_string(global_cfg, section ".muted",          nullptr); \
    if (hex) { double _a = 1.0; (ov)->has_muted = typio_parse_hex_color(hex, &(ov)->muted_r, &(ov)->muted_g, &(ov)->muted_b, &_a); } \
    hex = typio_config_get_string(global_cfg, section ".preedit",        nullptr); \
    if (hex) { double _a = 1.0; (ov)->has_preedit = typio_parse_hex_color(hex, &(ov)->preedit_r, &(ov)->preedit_g, &(ov)->preedit_b, &_a); } \
    hex = typio_config_get_string(global_cfg, section ".selection",      nullptr); \
    if (hex) { (ov)->has_selection = typio_parse_hex_color(hex, &(ov)->selection_r, &(ov)->selection_g, &(ov)->selection_b, &(ov)->selection_a); } \
    hex = typio_config_get_string(global_cfg, section ".selection_text", nullptr); \
    if (hex) { double _a = 1.0; (ov)->has_sel_text = typio_parse_hex_color(hex, &(ov)->sel_text_r, &(ov)->sel_text_g, &(ov)->sel_text_b, &_a); }

    LOAD_VARIANT("display.colors.light", &cfg->light_custom)
    LOAD_VARIANT("display.colors.dark",  &cfg->dark_custom)

#undef LOAD_VARIANT

build_descs:;
    /* label font: ~80% of candidate size, minimum 6pt */
    int label_size = cfg->font_size * 4 / 5;
    if (label_size < 6) label_size = 6;
    snprintf(cfg->font_desc,       sizeof(cfg->font_desc),
             "%s %d", cfg->font_family, cfg->font_size);
    snprintf(cfg->label_font_desc, sizeof(cfg->label_font_desc),
             "%s %d", cfg->font_family, label_size);
    snprintf(cfg->aux_font_desc,   sizeof(cfg->aux_font_desc),
             "%s %d", cfg->font_family,
             cfg->font_size > 6 ? cfg->font_size - 1 : 6);
}

void popup_config_build_palette(const PopupConfig *cfg,
                                 TypioCandidatePopupThemeCache *cache,
                                 TypioCandidatePopupPalette *out) {
    const TypioCandidatePopupPalette *base;
    const PopupThemeVariant *custom;

    if (!cfg || !cache || !out) return;

    /* 1. Resolve the built-in light or dark palette */
    base = typio_candidate_popup_theme_resolve(cache, cfg->theme_mode);
    *out = *base;

    /* 2. Apply user custom colors for the matching variant (higher priority) */
    custom = (base == typio_candidate_popup_palette_dark())
             ? &cfg->dark_custom : &cfg->light_custom;

    if (custom->has_bg)        { out->bg_r = custom->bg_r; out->bg_g = custom->bg_g; out->bg_b = custom->bg_b; out->bg_a = custom->bg_a; }
    if (custom->has_border)    { out->border_r = custom->border_r; out->border_g = custom->border_g; out->border_b = custom->border_b; out->border_a = custom->border_a; }
    if (custom->has_text)      { out->text_r = custom->text_r; out->text_g = custom->text_g; out->text_b = custom->text_b; }
    if (custom->has_muted)     { out->muted_r = custom->muted_r; out->muted_g = custom->muted_g; out->muted_b = custom->muted_b; }
    if (custom->has_preedit)   { out->preedit_r = custom->preedit_r; out->preedit_g = custom->preedit_g; out->preedit_b = custom->preedit_b; }
    if (custom->has_selection) { out->selection_r = custom->selection_r; out->selection_g = custom->selection_g; out->selection_b = custom->selection_b; out->selection_a = custom->selection_a; }
    if (custom->has_sel_text)  { out->selection_text_r = custom->sel_text_r; out->selection_text_g = custom->sel_text_g; out->selection_text_b = custom->sel_text_b; }
}
