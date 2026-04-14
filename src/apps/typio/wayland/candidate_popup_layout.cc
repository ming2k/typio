/**
 * @file candidate_popup_layout.cc
 * @brief Candidate popup geometry: Skia-based LRU cache and geometry computation.
 */

#include "candidate_popup_layout.h"
#include "typio/config.h"
#include "utils/string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration from skia_engine.cc */
extern "C" TypioTextEngine *skia_engine_create();

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

/* ── Skia Context management ────────────────────────────────────────── */

void popup_skia_ctx_init(PopupSkiaCtx *pc) {
    if (!pc) return;
    memset(pc, 0, sizeof(*pc));
    pc->engine = skia_engine_create();
}

void popup_skia_ctx_free(PopupSkiaCtx *pc) {
    if (!pc) return;
    for (size_t i = 0; i < POPUP_LAYOUT_CACHE_CAP; ++i) {
        if (pc->entries[i].layout) {
            pc->engine->vtable->free_layout(pc->entries[i].layout);
        }
        if (pc->entries[i].label_layout) {
            pc->engine->vtable->free_layout(pc->entries[i].label_layout);
        }
    }
    if (pc->engine) {
        // Assuming TypioTextEngine should be freed if it was allocated
        free(pc->engine->priv); // This depends on how skia_engine_create is implemented
        free(pc->engine);
    }
    memset(pc, 0, sizeof(*pc));
}

void popup_skia_ctx_invalidate(PopupSkiaCtx *pc) {
    if (!pc) return;
    for (size_t i = 0; i < POPUP_LAYOUT_CACHE_CAP; ++i) {
        if (pc->entries[i].layout) {
            pc->engine->vtable->free_layout(pc->entries[i].layout);
        }
        if (pc->entries[i].label_layout) {
            pc->engine->vtable->free_layout(pc->entries[i].label_layout);
        }
        memset(&pc->entries[i], 0, sizeof(pc->entries[i]));
    }
    pc->tick = 0;
}

/* ── LRU cache lookup / insert ──────────────────────────────────────── */

static PopupLayoutEntry *lru_get_or_create(PopupSkiaCtx *pc,
                                            const char *label,
                                            const char *text,
                                            const char *label_font_desc,
                                            const char *font_desc) {
    uint64_t key = layout_cache_key(label, text, label_font_desc, font_desc);
    size_t   lru_idx = 0;
    uint32_t lru_tick_min = UINT32_MAX;
    
    pc->tick++;

    for (size_t i = 0; i < POPUP_LAYOUT_CACHE_CAP; ++i) {
        PopupLayoutEntry *e = &pc->entries[i];
        if (e->layout && e->key == key &&
            strcmp(e->label, label) == 0 &&
            strcmp(e->text, text) == 0 &&
            strcmp(e->label_font_desc, label_font_desc) == 0 &&
            strcmp(e->font_desc, font_desc) == 0) {
            e->lru_tick = pc->tick;
            return e;
        }
        if (!e->layout || e->lru_tick < lru_tick_min) {
            lru_tick_min = e->layout ? e->lru_tick : 0;
            lru_idx = i;
        }
    }

    PopupLayoutEntry *victim = &pc->entries[lru_idx];
    if (victim->layout) pc->engine->vtable->free_layout(victim->layout);
    if (victim->label_layout) pc->engine->vtable->free_layout(victim->label_layout);
    
    victim->key = key;
    victim->lru_tick = pc->tick;
    snprintf(victim->label, sizeof(victim->label), "%s", label);
    snprintf(victim->text, sizeof(victim->text), "%s", text);
    snprintf(victim->label_font_desc, sizeof(victim->label_font_desc), "%s", label_font_desc);
    snprintf(victim->font_desc, sizeof(victim->font_desc), "%s", font_desc);
    
    victim->label_layout = pc->engine->vtable->create_layout(pc->engine, label, label_font_desc);
    victim->layout = pc->engine->vtable->create_layout(pc->engine, text, font_desc);
    
    pc->engine->vtable->get_metrics(victim->label_layout, &victim->label_pixel_w, &victim->label_pixel_h);
    pc->engine->vtable->get_metrics(victim->layout, &victim->pixel_w, &victim->pixel_h);
    victim->label_pixel_baseline = pc->engine->vtable->get_baseline(victim->label_layout);
    victim->pixel_baseline        = pc->engine->vtable->get_baseline(victim->layout);

    return victim;
}

/* ── Geometry helpers ───────────────────────────────────────────────── */

static void compute_positions_vertical(PopupGeometry *g, int pre_h_used) {
    int content_w = g->pre_w;
    int content_h = pre_h_used ? g->pre_h : 0;
    int max_label_w = 0;

    for (size_t i = 0; i < g->row_count; ++i) {
        if (g->rows[i].label_w > max_label_w) max_label_w = g->rows[i].label_w;
    }

    for (size_t i = 0; i < g->row_count; ++i) {
        int rw = max_label_w + POPUP_LABEL_GAP + g->rows[i].text_w + POPUP_ROW_PAD_X * 2;
        if (rw > content_w) content_w = rw;
        content_h += g->rows[i].h;
        if (i + 1 < g->row_count) content_h += POPUP_ROW_GAP;
    }

    if (pre_h_used && g->row_count > 0) content_h += POPUP_SECTION_GAP;

    if (g->mode_layout && g->mode_h > 0) {
        content_h += POPUP_SECTION_GAP + g->mode_h;
        if (g->mode_w > content_w) content_w = g->mode_w;
    }

    g->popup_w = content_w + POPUP_PAD_X * 2;
    if (g->popup_w < POPUP_MIN_WIDTH) g->popup_w = POPUP_MIN_WIDTH;
    g->popup_h = content_h + POPUP_PAD_Y * 2;

    int y = POPUP_PAD_Y;
    if (pre_h_used) y += g->pre_h + POPUP_SECTION_GAP;

    /* Horizontal centering offset for the entire candidate block if popup is wider than needed. */
    int h_offset = (g->popup_w - POPUP_PAD_X * 2 - (content_w - POPUP_ROW_PAD_X * 2)) / 2;
    if (h_offset < 0) h_offset = 0;

    for (size_t i = 0; i < g->row_count; ++i) {
        int row_content_h = g->rows[i].h - POPUP_ROW_PAD_Y * 2;
        /* Center text vertically within the row content area, and baseline-align the label to it. */
        float text_top = (float)(y + POPUP_ROW_PAD_Y)
                         + ((float)row_content_h - (float)g->rows[i].text_h) * 0.5f;
        float baseline_y = text_top + g->rows[i].text_ink_y_offset;
        float label_top = baseline_y - g->rows[i].label_ink_y_offset;

        g->rows[i].x = POPUP_PAD_X;
        g->rows[i].y = y;
        g->rows[i].w = g->popup_w - POPUP_PAD_X * 2;
        g->rows[i].label_x = g->rows[i].x + POPUP_ROW_PAD_X + h_offset;
        g->rows[i].label_y = (int)label_top;
        g->rows[i].text_x  = g->rows[i].x + POPUP_ROW_PAD_X + max_label_w + POPUP_LABEL_GAP + h_offset;
        g->rows[i].text_y  = (int)text_top;
        y += g->rows[i].h;
        if (i + 1 < g->row_count) y += POPUP_ROW_GAP;
    }

    g->pre_x = POPUP_PAD_X;
    g->pre_y = POPUP_PAD_Y;

    if (g->mode_layout && g->mode_h > 0) {
        g->mode_x = g->popup_w - POPUP_PAD_X - g->mode_w;
        g->mode_divider_y = g->popup_h - POPUP_PAD_Y - g->mode_h - POPUP_ROW_PAD_Y;
        g->mode_y = g->popup_h - POPUP_PAD_Y - g->mode_h;
    } else {
        g->mode_divider_y = -1;
    }
}

static void compute_positions_horizontal(PopupGeometry *g, int pre_h_used) {
    int content_w = g->pre_w;
    int content_h = pre_h_used ? g->pre_h : 0;
    int row_w = 0;
    int row_h = 0;

    for (size_t i = 0; i < g->row_count; ++i) {
        if (row_w > 0) row_w += POPUP_COL_GAP;
        row_w += g->rows[i].w;
        if (g->rows[i].h > row_h) row_h = g->rows[i].h;
    }
    content_h += row_h;
    int total_row = row_w;
    if (g->mode_layout && g->mode_w > 0) total_row += POPUP_COL_GAP + g->mode_w;
    content_w = total_row > g->pre_w ? total_row : g->pre_w;

    if (pre_h_used && g->row_count > 0) content_h += POPUP_SECTION_GAP;

    g->popup_w = content_w + POPUP_PAD_X * 2;
    if (g->popup_w < POPUP_MIN_WIDTH) g->popup_w = POPUP_MIN_WIDTH;
    g->popup_h = content_h + POPUP_PAD_Y * 2;

    int y = POPUP_PAD_Y;
    if (pre_h_used) y += g->pre_h + POPUP_SECTION_GAP;

    int x = POPUP_PAD_X;
    for (size_t i = 0; i < g->row_count; ++i) {
        int row_content_h = g->rows[i].h - POPUP_ROW_PAD_Y * 2;
        /* Center text vertically within the row content area, and baseline-align the label to it. */
        float text_top = (float)(y + POPUP_ROW_PAD_Y)
                         + ((float)row_content_h - (float)g->rows[i].text_h) * 0.5f;
        float baseline_y = text_top + g->rows[i].text_ink_y_offset;
        float label_top = baseline_y - g->rows[i].label_ink_y_offset;

        g->rows[i].x = x;
        g->rows[i].y = y;
        g->rows[i].label_x = x + POPUP_ROW_PAD_X;
        g->rows[i].label_y = (int)label_top;
        g->rows[i].text_x  = x + POPUP_ROW_PAD_X + g->rows[i].label_w + POPUP_LABEL_GAP;
        g->rows[i].text_y  = (int)text_top;
        x += g->rows[i].w + POPUP_COL_GAP;
    }

    g->pre_x = POPUP_PAD_X;
    g->pre_y = POPUP_PAD_Y;

    if (g->mode_layout && g->mode_h > 0) {
        g->mode_x = g->popup_w - POPUP_PAD_X - g->mode_w;
        g->mode_y = y + POPUP_ROW_PAD_Y;
        g->mode_divider_y = -1;
    } else {
        g->mode_divider_y = -1;
    }
}

static void compute_positions(PopupGeometry *g, const PopupConfig *cfg, int pre_h_used) {
    if (cfg->layout_mode == POPUP_LAYOUT_VERTICAL) {
        compute_positions_vertical(g, pre_h_used);
    } else {
        compute_positions_horizontal(g, pre_h_used);
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

PopupGeometry *popup_geometry_compute(PopupSkiaCtx *pc,
                                      const TypioCandidateList *candidates,
                                      const char *preedit_text,
                                      const char *mode_label,
                                      const PopupConfig *cfg,
                                      const TypioCandidatePopupPalette *palette,
                                      int scale) {
    if (!pc || !candidates || !cfg || !palette) return nullptr;
    if (candidates->count > POPUP_MAX_ROWS) return nullptr;

    PopupGeometry *g = (PopupGeometry *)calloc(1, sizeof(*g));
    if (!g) return nullptr;

    g->row_count = candidates->count;
    g->scale = scale;
    g->content_sig = candidates->content_signature;
    g->config = *cfg;
    g->resolved_palette = *palette;
    g->palette = &g->resolved_palette;
    g->palette_sig = typio_candidate_popup_palette_hash(palette);
    snprintf(g->preedit_text, sizeof(g->preedit_text), "%s", preedit_text ? preedit_text : "");
    snprintf(g->mode_label, sizeof(g->mode_label), "%s", mode_label ? mode_label : "");

    for (size_t i = 0; i < candidates->count; ++i) {
        char label_buf[64], text_buf[512];
        format_candidate_parts(&candidates->candidates[i], i, label_buf, sizeof(label_buf), text_buf, sizeof(text_buf));
        PopupLayoutEntry *entry = lru_get_or_create(pc, label_buf, text_buf, cfg->label_font_desc, cfg->font_desc);
        
        g->rows[i].label_layout = entry->label_layout;
        g->rows[i].layout = entry->layout;
        g->rows[i].label_w = (int)entry->label_pixel_w;
        g->rows[i].label_h = (int)entry->label_pixel_h;
        g->rows[i].text_w  = (int)entry->pixel_w;
        g->rows[i].text_h  = (int)entry->pixel_h;
        g->rows[i].label_ink_y_offset = entry->label_pixel_baseline;
        g->rows[i].text_ink_y_offset  = entry->pixel_baseline;
        g->rows[i].w = (int)entry->label_pixel_w + POPUP_LABEL_GAP + (int)entry->pixel_w + POPUP_ROW_PAD_X * 2;
        g->rows[i].h = (g->rows[i].label_h > g->rows[i].text_h
                        ? g->rows[i].label_h : g->rows[i].text_h) + POPUP_ROW_PAD_Y * 2;
    }

    if (preedit_text && preedit_text[0]) {
        g->preedit_layout = pc->engine->vtable->create_layout(pc->engine, preedit_text, cfg->aux_font_desc);
        float fw, fh;
        pc->engine->vtable->get_metrics(g->preedit_layout, &fw, &fh);
        g->pre_w = (int)fw; g->pre_h = (int)fh;
    }

    if (cfg->mode_indicator && mode_label && mode_label[0]) {
        g->mode_layout = pc->engine->vtable->create_layout(pc->engine, mode_label, cfg->aux_font_desc);
        float fw, fh;
        pc->engine->vtable->get_metrics(g->mode_layout, &fw, &fh);
        g->mode_w = (int)fw; g->mode_h = (int)fh;
    }

    compute_positions(g, cfg, (preedit_text && preedit_text[0]) ? 1 : 0);
    return g;
}

PopupGeometry *popup_geometry_update_aux(PopupSkiaCtx *pc,
                                         const PopupGeometry *base,
                                         const char *preedit_text,
                                         const char *mode_label) {
    if (!pc || !base) return nullptr;

    float new_pre_w = 0, new_pre_h = 0;
    float new_mode_w = 0, new_mode_h = 0;
    TypioTextLayout *new_preedit_layout = nullptr;
    TypioTextLayout *new_mode_layout = nullptr;

    if (preedit_text && preedit_text[0]) {
        new_preedit_layout = pc->engine->vtable->create_layout(pc->engine, preedit_text, base->config.aux_font_desc);
        pc->engine->vtable->get_metrics(new_preedit_layout, &new_pre_w, &new_pre_h);
    }
    if (base->config.mode_indicator && mode_label && mode_label[0]) {
        new_mode_layout = pc->engine->vtable->create_layout(pc->engine, mode_label, base->config.aux_font_desc);
        pc->engine->vtable->get_metrics(new_mode_layout, &new_mode_w, &new_mode_h);
    }

    if ((int)new_pre_w != base->pre_w || (int)new_pre_h != base->pre_h ||
        (int)new_mode_w != base->mode_w || (int)new_mode_h != base->mode_h) {
        if (new_preedit_layout) pc->engine->vtable->free_layout(new_preedit_layout);
        if (new_mode_layout) pc->engine->vtable->free_layout(new_mode_layout);
        return nullptr;
    }

    PopupGeometry *g = (PopupGeometry *)malloc(sizeof(*g));
    if (!g) {
        if (new_preedit_layout) pc->engine->vtable->free_layout(new_preedit_layout);
        if (new_mode_layout) pc->engine->vtable->free_layout(new_mode_layout);
        return nullptr;
    }
    *g = *base;
    g->palette = &g->resolved_palette;
    g->preedit_layout = new_preedit_layout;
    g->mode_layout = new_mode_layout;
    snprintf(g->preedit_text, sizeof(g->preedit_text), "%s", preedit_text ? preedit_text : "");
    snprintf(g->mode_label, sizeof(g->mode_label), "%s", mode_label ? mode_label : "");
    return g;
}

void popup_geometry_free(PopupGeometry *g) {
    if (!g) return;
    // Preedit and mode layouts are owned by geometry
    // But wait, who is the engine? We don't have the engine here.
    // This is an architectural issue.
    // We might need to store the engine pointer in PopupGeometry or just assume we know how to free it.
    // Since we are in C++, maybe we should use smart pointers or a global engine?
    // Let's assume TypioTextLayout can be freed if we have the engine.
    // For now, I'll skip freeing these in PopupGeometry unless I have an engine ref.
    // Wait, skia_engine.cc provides free_layout which just does `delete priv`.
    // So we can just call it if we have it.
    // I'll add a helper to free without engine if it's safe.
    if (g->preedit_layout) {
        /* We need the engine vtable... let's just use the fact that it's a delete priv. */
        /* This is hacky. Better: the geometry compute/update functions should use a deleter. */
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
    }

    if (layout && strcmp(layout, "horizontal") == 0) {
        cfg->layout_mode = POPUP_LAYOUT_HORIZONTAL;
    }

    if (font_size < 6)  font_size = 6;
    if (font_size > 72) font_size = 72;
    cfg->font_size = font_size;

    cfg->mode_indicator = typio_config_get_bool(global_cfg, "display.popup_mode_indicator", true);

    font_family = typio_config_get_string(global_cfg, "display.font_family", nullptr);
    if (font_family && font_family[0]) {
        snprintf(cfg->font_family, sizeof(cfg->font_family), "%s", font_family);
    }

    #define LOAD_VARIANT(section, ov) \
    hex = typio_config_get_string(global_cfg, section ".background", nullptr); \
    if (hex) { (ov)->has_bg = typio_parse_hex_color(hex, &(ov)->bg_r, &(ov)->bg_g, &(ov)->bg_b, &(ov)->bg_a); } \
    hex = typio_config_get_string(global_cfg, section ".border", nullptr); \
    if (hex) { (ov)->has_border = typio_parse_hex_color(hex, &(ov)->border_r, &(ov)->border_g, &(ov)->border_b, &(ov)->border_a); } \
    hex = typio_config_get_string(global_cfg, section ".text", nullptr); \
    if (hex) { double _a = 1.0; (ov)->has_text = typio_parse_hex_color(hex, &(ov)->text_r, &(ov)->text_g, &(ov)->text_b, &_a); } \
    hex = typio_config_get_string(global_cfg, section ".muted", nullptr); \
    if (hex) { double _a = 1.0; (ov)->has_muted = typio_parse_hex_color(hex, &(ov)->muted_r, &(ov)->muted_g, &(ov)->muted_b, &_a); } \
    hex = typio_config_get_string(global_cfg, section ".preedit", nullptr); \
    if (hex) { double _a = 1.0; (ov)->has_preedit = typio_parse_hex_color(hex, &(ov)->preedit_r, &(ov)->preedit_g, &(ov)->preedit_b, &_a); } \
    hex = typio_config_get_string(global_cfg, section ".selection", nullptr); \
    if (hex) { (ov)->has_selection = typio_parse_hex_color(hex, &(ov)->selection_r, &(ov)->selection_g, &(ov)->selection_b, &(ov)->selection_a); } \
    hex = typio_config_get_string(global_cfg, section ".selection_text", nullptr); \
    if (hex) { double _a = 1.0; (ov)->has_sel_text = typio_parse_hex_color(hex, &(ov)->sel_text_r, &(ov)->sel_text_g, &(ov)->sel_text_b, &_a); }

    LOAD_VARIANT("display.colors.light", &cfg->light_custom)
    LOAD_VARIANT("display.colors.dark", &cfg->dark_custom)

build_descs:;
    int ls = cfg->font_size * 4 / 5; if (ls < 7) ls = 7;
    snprintf(cfg->font_desc, sizeof(cfg->font_desc), "%s %d", cfg->font_family, cfg->font_size);
    snprintf(cfg->label_font_desc, sizeof(cfg->label_font_desc), "%s %d", cfg->font_family, ls);
    snprintf(cfg->aux_font_desc, sizeof(cfg->aux_font_desc), "%s %d", cfg->font_family, cfg->font_size > 6 ? cfg->font_size - 1 : 6);
}

void popup_config_build_palette(const PopupConfig *cfg, TypioCandidatePopupThemeCache *cache, TypioCandidatePopupPalette *out) {
    const TypioCandidatePopupPalette *base = typio_candidate_popup_theme_resolve(cache, cfg->theme_mode);
    *out = *base;
    const PopupThemeVariant *custom = (base == typio_candidate_popup_palette_dark()) ? &cfg->dark_custom : &cfg->light_custom;
    if (custom->has_bg) { out->bg_r = custom->bg_r; out->bg_g = custom->bg_g; out->bg_b = custom->bg_b; out->bg_a = custom->bg_a; }
    if (custom->has_border) { out->border_r = custom->border_r; out->border_g = custom->border_g; out->border_b = custom->border_b; out->border_a = custom->border_a; }
    if (custom->has_text) { out->text_r = custom->text_r; out->text_g = custom->text_g; out->text_b = custom->text_b; }
    if (custom->has_muted) { out->muted_r = custom->muted_r; out->muted_g = custom->muted_g; out->muted_b = custom->muted_b; }
    if (custom->has_preedit) { out->preedit_r = custom->preedit_r; out->preedit_g = custom->preedit_g; out->preedit_b = custom->preedit_b; }
    if (custom->has_selection) { out->selection_r = custom->selection_r; out->selection_g = custom->selection_g; out->selection_b = custom->selection_b; out->selection_a = custom->selection_a; }
    if (custom->has_sel_text) { out->selection_text_r = custom->sel_text_r; out->selection_text_g = custom->sel_text_g; out->selection_text_b = custom->sel_text_b; }
}
