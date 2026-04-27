#include "internal.h"
#include <math.h>

static fx_matrix canvas_transform(const fx_canvas *c)
{
    float dpr = (c->dpr > 0.0f) ? c->dpr : 1.0f;
    fx_matrix m = c->current_matrix;
    if (dpr != 1.0f) {
        fx_matrix dpr_m = { .m = { dpr, 0, 0, dpr, 0, 0 } };
        fx_matrix combined;
        fx_matrix_multiply(&combined, &m, &dpr_m);
        m = combined;
    }
    return m;
}

static fx_rect scale_rect_by_dpr(const fx_rect *r, float dpr)
{
    if (!r) return (fx_rect){0};
    if (dpr <= 0.0f || dpr == 1.0f) return *r;
    return (fx_rect){ r->x * dpr, r->y * dpr, r->w * dpr, r->h * dpr };
}

static bool ensure_op_capacity(fx_canvas *c, size_t extra)
{
    size_t need = c->op_count + extra;
    if (need <= c->op_cap) return true;

    size_t new_cap = c->op_cap ? c->op_cap : 16;
    while (new_cap < need) new_cap *= 2;

    fx_op *ops = realloc(c->ops, new_cap * sizeof(*ops));
    if (!ops) return false;

    c->ops = ops;
    c->op_cap = new_cap;
    return true;
}

static bool push_op(fx_canvas *c, const fx_op *op)
{
    if (!c || !op) return false;
    if (!ensure_op_capacity(c, 1)) return false;
    c->ops[c->op_count++] = *op;
    return true;
}

void fx_canvas_reset(fx_canvas *c)
{
    if (!c) return;

    for (size_t i = 0; i < c->op_count; ++i) {
        fx_op *op = &c->ops[i];
        if (op->kind == FX_OP_FILL_PATH && op->u.fill_path.owns_path) {
            fx_path_destroy((fx_path *)op->u.fill_path.path);
        } else if (op->kind == FX_OP_STROKE_PATH && op->u.stroke_path.owns_path) {
            fx_path_destroy((fx_path *)op->u.stroke_path.path);
        }
    }

    c->has_clear = false;
    c->clear_color = 0;
    c->op_count = 0;

    c->state_count = 0;
    fx_matrix_identity(&c->current_matrix);
    c->dpr = 1.0f;
}

void fx_canvas_dispose(fx_canvas *c)
{
    if (!c) return;
    fx_canvas_reset(c);
    free(c->ops);
    free(c->state_stack);
    memset(c, 0, sizeof(*c));
}

void fx_clear(fx_canvas *c, fx_color color)
{
    if (!c) return;
    c->clear_color = color;
    c->has_clear = true;
}

size_t fx_canvas_op_count(const fx_canvas *c)
{
    return c ? c->op_count : 0;
}

void fx_paint_init(fx_paint *paint, fx_color color)
{
    if (!paint) return;
    paint->color = color;
    paint->stroke_width = 1.0f;
    paint->miter_limit = 4.0f;
    paint->line_cap = FX_CAP_BUTT;
    paint->line_join = FX_JOIN_MITER;
    paint->gradient = NULL;
}

void fx_paint_set_gradient(fx_paint *paint, fx_gradient *gradient)
{
    if (!paint) return;
    paint->gradient = gradient;
}

bool fx_fill_rect(fx_canvas *c, const fx_rect *rect, fx_color color)
{
    if (!c || !rect) return false;
    fx_rect r = scale_rect_by_dpr(rect, c->dpr);

    fx_matrix m = canvas_transform(c);
    if (!fx_matrix_is_identity(&m)) {
        /* Transformed rect may not be axis-aligned; fall back to path fill. */
        float x0 = r.x,         y0 = r.y;
        float x1 = r.x + r.w,   y1 = r.y;
        float x2 = r.x + r.w,   y2 = r.y + r.h;
        float x3 = r.x,         y3 = r.y + r.h;
        fx_matrix_transform_point(&m, &x0, &y0);
        fx_matrix_transform_point(&m, &x1, &y1);
        fx_matrix_transform_point(&m, &x2, &y2);
        fx_matrix_transform_point(&m, &x3, &y3);
        fx_path *path = fx_path_create();
        if (!path) return false;
        fx_path_move_to(path, x0, y0);
        fx_path_line_to(path, x1, y1);
        fx_path_line_to(path, x2, y2);
        fx_path_line_to(path, x3, y3);
        fx_path_close(path);
        fx_paint p;
        fx_paint_init(&p, color);
        fx_op op = {
            .kind = FX_OP_FILL_PATH,
            .u.fill_path = {
                .path = path,
                .paint = p,
                .owns_path = true,
            },
        };
        bool ok = push_op(c, &op);
        if (!ok) fx_path_destroy(path);
        return ok;
    }

    fx_op op = {
        .kind = FX_OP_FILL_RECT,
        .u.fill_rect = {
            .rect = r,
            .color = color,
        },
    };
    return push_op(c, &op);
}

bool fx_fill_path(fx_canvas *c, const fx_path *path, const fx_paint *paint)
{
    if (!c || !path || !paint) return false;
    fx_op op = {
        .kind = FX_OP_FILL_PATH,
        .u.fill_path = {
            .path = path,
            .paint = *paint,
            .owns_path = false,
        },
    };

    fx_matrix m = canvas_transform(c);
    if (!fx_matrix_is_identity(&m)) {
        op.u.fill_path.path = fx_path_transform(path, &m);
        op.u.fill_path.owns_path = true;
        if (!op.u.fill_path.path) return false;
    }

    return push_op(c, &op);
}

bool fx_stroke_path(fx_canvas *c, const fx_path *path, const fx_paint *paint)
{
    if (!c || !path || !paint || paint->stroke_width <= 0.0f) return false;
    fx_op op = {
        .kind = FX_OP_STROKE_PATH,
        .u.stroke_path = {
            .path = path,
            .paint = *paint,
            .owns_path = false,
        },
    };

    fx_matrix m = canvas_transform(c);
    if (!fx_matrix_is_identity(&m)) {
        op.u.stroke_path.path = fx_path_transform(path, &m);
        op.u.stroke_path.owns_path = true;
        if (!op.u.stroke_path.path) return false;
        /* Note: stroke width is not transformed here; it follows
         * the convention of being in 'user space' but applied to
         * a path that was transformed. A more complete impl would
         * scale the width too. */
    }

    return push_op(c, &op);
}

bool fx_draw_image(fx_canvas *c, const fx_image *image,
                   const fx_rect *src, const fx_rect *dst)
{
    fx_rect full_src = { 0 };
    if (!c || !image || !dst) return false;
    if (!fx_image_get_desc(image, NULL)) return false;

    fx_rect scaled_dst = scale_rect_by_dpr(dst, c->dpr);
    fx_op op = {
        .kind = FX_OP_DRAW_IMAGE,
        .u.draw_image = {
            .image = image,
            .src = { 0 },
            .dst = scaled_dst,
        },
    };

    if (!src) {
        fx_image_desc desc;
        if (!fx_image_get_desc(image, &desc)) return false;
        full_src = (fx_rect){
            .x = 0.0f,
            .y = 0.0f,
            .w = (float)desc.width,
            .h = (float)desc.height,
        };
        op.u.draw_image.src = full_src;
    } else {
        op.u.draw_image.src = *src;
    }

    return push_op(c, &op);
}

static void fx_color_to_floats(fx_color c, float out[4])
{
    out[0] = ((c >> 16) & 0xFF) / 255.0f;
    out[1] = ((c >>  8) & 0xFF) / 255.0f;
    out[2] = ((c      ) & 0xFF) / 255.0f;
    out[3] = ((c >> 24) & 0xFF) / 255.0f;
}

fx_gradient *fx_gradient_create_linear(fx_context *ctx,
                                       const fx_linear_gradient_desc *desc)
{
    if (!ctx || !desc || desc->stop_count < 2 || desc->stop_count > 4)
        return NULL;
    fx_gradient *g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->ctx = ctx;
    g->mode = 0;
    g->start[0] = desc->start.x;
    g->start[1] = desc->start.y;
    g->end[0]   = desc->end.x;
    g->end[1]   = desc->end.y;
    g->stop_count = desc->stop_count;
    for (uint32_t i = 0; i < desc->stop_count; ++i) {
        fx_color_to_floats(desc->colors[i], g->colors[i]);
        g->stops[i] = desc->stops[i];
    }
    return g;
}

fx_gradient *fx_gradient_create_radial(fx_context *ctx,
                                       const fx_radial_gradient_desc *desc)
{
    if (!ctx || !desc || desc->stop_count < 2 || desc->stop_count > 4)
        return NULL;
    fx_gradient *g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->ctx = ctx;
    g->mode = 1;
    g->start[0] = desc->center.x;
    g->start[1] = desc->center.y;
    g->end[0]   = desc->radius;
    g->end[1]   = 0.0f;
    g->stop_count = desc->stop_count;
    for (uint32_t i = 0; i < desc->stop_count; ++i) {
        fx_color_to_floats(desc->colors[i], g->colors[i]);
        g->stops[i] = desc->stops[i];
    }
    return g;
}

void fx_gradient_destroy(fx_gradient *gradient)
{
    if (!gradient) return;
    free(gradient);
}

void fx_clip_rect(fx_canvas *c, const fx_rect *rect)
{
    if (!c || !rect) return;

    fx_rect r = scale_rect_by_dpr(rect, c->dpr);
    fx_matrix m = canvas_transform(c);
    if (!fx_matrix_is_identity(&m)) {
        float x0 = r.x, y0 = r.y;
        float x1 = r.x + r.w, y1 = r.y + r.h;
        float x2 = r.x + r.w, y2 = r.y;
        float x3 = r.x, y3 = r.y + r.h;
        fx_matrix_transform_point(&m, &x0, &y0);
        fx_matrix_transform_point(&m, &x1, &y1);
        fx_matrix_transform_point(&m, &x2, &y2);
        fx_matrix_transform_point(&m, &x3, &y3);
        float minx = x0, maxx = x0, miny = y0, maxy = y0;
        if (x1 < minx) minx = x1;
        if (x1 > maxx) maxx = x1;
        if (y1 < miny) miny = y1;
        if (y1 > maxy) maxy = y1;
        if (x2 < minx) minx = x2;
        if (x2 > maxx) maxx = x2;
        if (y2 < miny) miny = y2;
        if (y2 > maxy) maxy = y2;
        if (x3 < minx) minx = x3;
        if (x3 > maxx) maxx = x3;
        if (y3 < miny) miny = y3;
        if (y3 > maxy) maxy = y3;
        r.x = minx; r.y = miny;
        r.w = maxx - minx;
        r.h = maxy - miny;
    }

    fx_op op = {
        .kind = FX_OP_CLIP_RECT,
        .u.clip_rect = { .rect = r },
    };
    push_op(c, &op);
}

void fx_reset_clip(fx_canvas *c)
{
    if (!c) return;
    fx_op op = { .kind = FX_OP_RESET_CLIP };
    push_op(c, &op);
}

void fx_clip_path(fx_canvas *c, const fx_path *path)
{
    if (!c || !path) return;

    fx_op op = {
        .kind = FX_OP_CLIP_PATH,
        .u.clip_path = {
            .path = path,
            .paint = {0},
            .owns_path = false,
        },
    };

    fx_matrix m = canvas_transform(c);
    if (!fx_matrix_is_identity(&m)) {
        op.u.clip_path.path = fx_path_transform(path, &m);
        op.u.clip_path.owns_path = true;
        if (!op.u.clip_path.path) return;
    }

    push_op(c, &op);
}

bool fx_draw_glyph_run(fx_canvas *c, const fx_font *font,
                       const fx_glyph_run *run,
                       float x, float y, const fx_paint *paint)
{
    if (!c || !font || !run || !paint || fx_glyph_run_count(run) == 0) return false;

    x *= c->dpr;
    y *= c->dpr;
    fx_matrix_transform_point(&c->current_matrix, &x, &y);

    fx_op op = {
        .kind = FX_OP_DRAW_GLYPHS,
        .u.draw_glyphs = {
            .font = font,
            .run = run,
            .x = x,
            .y = y,
            .paint = *paint,
        },
    };
    return push_op(c, &op);
}

/* ---------- canvas state & transforms ---------- */

void fx_save(fx_canvas *c)
{
    if (!c) return;

    if (c->state_count + 1 > c->state_cap) {
        size_t new_cap = c->state_cap ? c->state_cap * 2 : 4;
        fx_matrix *new_stack = realloc(c->state_stack, new_cap * sizeof(fx_matrix));
        if (!new_stack) return;
        c->state_stack = new_stack;
        c->state_cap = new_cap;
    }

    c->state_stack[c->state_count++] = c->current_matrix;
}

void fx_restore(fx_canvas *c)
{
    if (!c || c->state_count == 0) return;
    c->current_matrix = c->state_stack[--c->state_count];
}

void fx_translate(fx_canvas *c, float dx, float dy)
{
    if (!c) return;
    fx_matrix m;
    fx_matrix_identity(&m);
    m.m[4] = dx;
    m.m[5] = dy;
    fx_concat(c, &m);
}

void fx_scale(fx_canvas *c, float sx, float sy)
{
    if (!c) return;
    fx_matrix m;
    fx_matrix_identity(&m);
    m.m[0] = sx;
    m.m[3] = sy;
    fx_concat(c, &m);
}

void fx_rotate(fx_canvas *c, float radians)
{
    if (!c) return;
    float s = sinf(radians);
    float cc = cosf(radians);
    fx_matrix m;
    m.m[0] = cc;  m.m[2] = -s; m.m[4] = 0.0f;
    m.m[1] = s;   m.m[3] = cc; m.m[5] = 0.0f;
    fx_concat(c, &m);
}

void fx_concat(fx_canvas *c, const fx_matrix *m)
{
    if (!c || !m) return;
    fx_matrix next;
    fx_matrix_multiply(&next, &c->current_matrix, m);
    c->current_matrix = next;
}

void fx_set_matrix(fx_canvas *c, const fx_matrix *m)
{
    if (!c || !m) return;
    c->current_matrix = *m;
}

void fx_get_matrix(const fx_canvas *c, fx_matrix *out_m)
{
    if (!c || !out_m) return;
    *out_m = c->current_matrix;
}
