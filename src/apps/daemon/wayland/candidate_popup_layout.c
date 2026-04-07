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

static uint64_t layout_cache_key(const char *text, const char *font_desc) {
    uint64_t h = 14695981039346656037ULL;
    h = fnv1a(h, text);
    h ^= 0xffULL;
    h *= 1099511628211ULL;
    h = fnv1a(h, font_desc);
    return h;
}

/* ── Candidate text formatting ──────────────────────────────────────── */

static void format_candidate(const TypioCandidate *c, size_t idx,
                              char *buf, size_t buf_size) {
    char fallback_label[32];
    const char *label;

    if (!c || !c->text) {
        buf[0] = '\0';
        return;
    }

    if (c->label && c->label[0]) {
        label = c->label;
    } else {
        snprintf(fallback_label, sizeof(fallback_label), "%zu", idx + 1);
        label = fallback_label;
    }

    if (c->comment && c->comment[0]) {
        snprintf(buf, buf_size, "%s. %s  %s", label, c->text, c->comment);
    } else {
        snprintf(buf, buf_size, "%s. %s", label, c->text);
    }
}

/* ── PangoContext management ────────────────────────────────────────── */

static void pango_ctx_rebuild_fonts(PopupPangoCtx *pc) {
    if (pc->font) {
        pango_font_description_free(pc->font);
        pc->font = nullptr;
    }
    if (pc->aux_font) {
        pango_font_description_free(pc->aux_font);
        pc->aux_font = nullptr;
    }

    if (pc->font_desc[0]) {
        pc->font = pango_font_description_from_string(pc->font_desc);
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
    }

    if (pc->font) {
        pango_font_description_free(pc->font);
        pc->font = nullptr;
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
        memset(&pc->entries[i], 0, sizeof(pc->entries[i]));
    }
    pc->tick = 0;

    pango_ctx_rebuild_fonts(pc);
}

/* ── Font cache helpers ─────────────────────────────────────────────── */

static bool pango_ctx_ensure_font(PopupPangoCtx *pc,
                                   const char *font_desc,
                                   const char *aux_font_desc) {
    bool changed = false;

    if (!pc || !pc->ctx) return false;

    if (strcmp(pc->font_desc, font_desc ? font_desc : "") != 0) {
        snprintf(pc->font_desc, sizeof(pc->font_desc),
                 "%s", font_desc ? font_desc : "");
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
    if (!pc->aux_font && pc->aux_font_desc[0]) {
        pc->aux_font = pango_font_description_from_string(pc->aux_font_desc);
    }

    return true;
}

/* ── LRU cache lookup / insert ──────────────────────────────────────── */

/**
 * Find a cached layout for (text, font_desc).
 * On hit: update lru_tick and return the entry.
 * On miss: evict the LRU entry, create a new layout, return the entry.
 * Returns NULL only if PangoContext is unavailable.
 */
static PopupLayoutEntry *lru_get_or_create(PopupPangoCtx *pc,
                                            const char *text,
                                            PangoFontDescription *font,
                                            const char *font_desc) {
    uint64_t key;
    size_t   i;
    size_t   lru_idx  = 0;
    uint32_t lru_tick = UINT32_MAX;
    PangoLayout *layout;

    if (!pc || !pc->ctx || !text || !font) return nullptr;

    key = layout_cache_key(text, font_desc ? font_desc : "");
    pc->tick++;

    /* Linear scan: find hit or identify LRU victim */
    for (i = 0; i < POPUP_LAYOUT_CACHE_CAP; ++i) {
        PopupLayoutEntry *e = &pc->entries[i];

        if (e->layout && e->key == key &&
            strcmp(e->text, text) == 0 &&
            strcmp(e->font_desc, font_desc ? font_desc : "") == 0) {
            /* Cache hit */
            e->lru_tick = pc->tick;
            return e;
        }

        if (!e->layout || e->lru_tick < lru_tick) {
            lru_tick = e->layout ? e->lru_tick : 0;
            lru_idx  = i;
        }
    }

    /* Cache miss: evict LRU slot */
    PopupLayoutEntry *victim = &pc->entries[lru_idx];
    if (victim->layout) {
        g_object_unref(victim->layout);
        victim->layout = nullptr;
    }

    layout = pango_layout_new(pc->ctx);
    if (!layout) return nullptr;

    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text, -1);
    pango_layout_get_pixel_size(layout, &victim->pixel_w, &victim->pixel_h);

    victim->key      = key;
    victim->layout   = layout;
    victim->lru_tick = pc->tick;
    snprintf(victim->text,      sizeof(victim->text),      "%s", text);
    snprintf(victim->font_desc, sizeof(victim->font_desc), "%s",
             font_desc ? font_desc : "");

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

/* Compute the popup width and height, and assign row positions. */
static void compute_positions(PopupGeometry *g,
                               const PopupConfig *cfg,
                               int pre_h_used) {
    int content_w = g->pre_w;
    int content_h = pre_h_used ? g->pre_h : 0;
    int row_w     = 0;
    int row_h     = 0;
    size_t i;

    /* Measure content area */
    if (cfg->layout_mode == POPUP_LAYOUT_VERTICAL) {
        for (i = 0; i < g->row_count; ++i) {
            if (g->rows[i].w > content_w) content_w = g->rows[i].w;
            content_h += g->rows[i].h;
            if (i + 1 < g->row_count) content_h += POPUP_ROW_GAP;
        }
    } else {
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
    }

    if (pre_h_used && g->row_count > 0) content_h += POPUP_SECTION_GAP;

    /* Mode label (vertical layout only adds to height here) */
    if (g->mode_layout && g->mode_h > 0 &&
        cfg->layout_mode == POPUP_LAYOUT_VERTICAL) {
        content_h += POPUP_SECTION_GAP + g->mode_h;
        if (g->mode_w > content_w) content_w = g->mode_w;
    }

    g->popup_w = content_w + POPUP_PADDING * 2;
    if (g->popup_w < POPUP_MIN_WIDTH) g->popup_w = POPUP_MIN_WIDTH;
    g->popup_h = content_h + POPUP_PADDING * 2;

    /* Row positions */
    int y = POPUP_PADDING;
    if (pre_h_used) y += g->pre_h + POPUP_SECTION_GAP;

    if (cfg->layout_mode == POPUP_LAYOUT_VERTICAL) {
        for (i = 0; i < g->row_count; ++i) {
            g->rows[i].x      = POPUP_PADDING;
            g->rows[i].y      = y;
            g->rows[i].w      = g->popup_w - POPUP_PADDING * 2;
            g->rows[i].text_x = g->rows[i].x + POPUP_ROW_PAD_X;
            g->rows[i].text_y = g->rows[i].y + POPUP_ROW_PAD_Y;
            y += g->rows[i].h;
            if (i + 1 < g->row_count) y += POPUP_ROW_GAP;
        }
    } else {
        int x = POPUP_PADDING;
        for (i = 0; i < g->row_count; ++i) {
            g->rows[i].x      = x;
            g->rows[i].y      = y;
            g->rows[i].text_x = x + POPUP_ROW_PAD_X;
            g->rows[i].text_y = y + POPUP_ROW_PAD_Y;
            x += g->rows[i].w + POPUP_COL_GAP;
        }
    }

    /* Preedit position */
    g->pre_x = POPUP_PADDING;
    g->pre_y = POPUP_PADDING;

    /* Mode label position */
    if (g->mode_layout && g->mode_h > 0) {
        g->mode_x = g->popup_w - POPUP_PADDING - g->mode_w;
        if (cfg->layout_mode == POPUP_LAYOUT_HORIZONTAL) {
            g->mode_y        = y + POPUP_ROW_PAD_Y;
            g->mode_divider_y = -1;
        } else {
            g->mode_divider_y = g->popup_h - POPUP_PADDING -
                                g->mode_h - POPUP_ROW_PAD_Y;
            g->mode_y        = g->popup_h - POPUP_PADDING - g->mode_h;
        }
    } else {
        g->mode_divider_y = -1;
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
    if (!pango_ctx_ensure_font(pc, cfg->font_desc, cfg->aux_font_desc)) {
        return nullptr;
    }

    g = calloc(1, sizeof(*g));
    if (!g) return nullptr;

    g->row_count       = candidates->count;
    g->scale           = scale;
    g->content_sig     = candidates->content_signature;
    g->palette         = palette;
    g->config          = *cfg;
    g->mode_divider_y  = -1;

    snprintf(g->preedit_text, sizeof(g->preedit_text),
             "%s", preedit_text ? preedit_text : "");
    snprintf(g->mode_label, sizeof(g->mode_label),
             "%s", mode_label ? mode_label : "");

    /* Measure each candidate row via the LRU cache */
    for (i = 0; i < candidates->count; ++i) {
        char formatted[512];
        PopupLayoutEntry *entry;

        format_candidate(&candidates->candidates[i], i,
                         formatted, sizeof(formatted));

        entry = lru_get_or_create(pc, formatted, pc->font, cfg->font_desc);
        if (!entry) {
            free(g);
            return nullptr;
        }

        g->rows[i].layout  = entry->layout;
        g->rows[i].text_w  = entry->pixel_w;
        g->rows[i].text_h  = entry->pixel_h;
        g->rows[i].w       = entry->pixel_w + POPUP_ROW_PAD_X * 2;
        g->rows[i].h       = entry->pixel_h + POPUP_ROW_PAD_Y * 2;
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
    int          font_size;

    if (!cfg) return;

    cfg->theme_mode    = TYPIO_CANDIDATE_POPUP_THEME_AUTO;
    cfg->layout_mode   = POPUP_LAYOUT_VERTICAL;
    cfg->font_size     = POPUP_DEFAULT_FONT_SIZE;
    cfg->mode_indicator = true;

    global_cfg = instance ? typio_instance_get_config(instance) : nullptr;
    if (global_cfg) {
        theme  = typio_config_get_string(global_cfg, "display.popup_theme",       nullptr);
        layout = typio_config_get_string(global_cfg, "display.candidate_layout",  nullptr);
        font_size = typio_config_get_int(global_cfg, "display.font_size",
                                          POPUP_DEFAULT_FONT_SIZE);

        if (theme && strcmp(theme, "dark") == 0) {
            cfg->theme_mode = TYPIO_CANDIDATE_POPUP_THEME_DARK;
        } else if (theme && strcmp(theme, "light") == 0) {
            cfg->theme_mode = TYPIO_CANDIDATE_POPUP_THEME_LIGHT;
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
    }

    snprintf(cfg->font_desc,     sizeof(cfg->font_desc),     "Sans %d", cfg->font_size);
    snprintf(cfg->aux_font_desc, sizeof(cfg->aux_font_desc), "Sans %d",
             cfg->font_size > 6 ? cfg->font_size - 1 : 6);
}
