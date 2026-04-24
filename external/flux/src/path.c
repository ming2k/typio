#include "internal.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

enum {
    FX_PATH_MOVE = 0,
    FX_PATH_LINE = 1,
    FX_PATH_QUAD = 2,
    FX_PATH_CUBIC = 3,
    FX_PATH_CLOSE = 4,
};

#define FX_FLATTEN_MAX_DEPTH 16

static bool ensure_verb_capacity(fx_path *path, size_t extra)
{
    size_t need = path->verb_count + extra;
    if (need <= path->verb_cap) return true;

    size_t new_cap = path->verb_cap ? path->verb_cap : 16;
    while (new_cap < need) new_cap *= 2;

    uint8_t *verbs = realloc(path->verbs, new_cap * sizeof(*verbs));
    if (!verbs) return false;

    path->verbs = verbs;
    path->verb_cap = new_cap;
    return true;
}

static bool ensure_point_capacity(fx_path *path, size_t extra)
{
    size_t need = path->point_count + extra;
    if (need <= path->point_cap) return true;

    size_t new_cap = path->point_cap ? path->point_cap : 32;
    while (new_cap < need) new_cap *= 2;

    fx_point *points = realloc(path->points, new_cap * sizeof(*points));
    if (!points) return false;

    path->points = points;
    path->point_cap = new_cap;
    return true;
}

static bool ensure_temp_point_capacity(fx_point **points,
                                       size_t *count,
                                       size_t *cap,
                                       size_t extra)
{
    size_t need = *count + extra;
    if (need <= *cap) return true;

    size_t new_cap = *cap ? *cap : 32;
    while (new_cap < need) new_cap *= 2;

    fx_point *grown = realloc(*points, new_cap * sizeof(*grown));
    if (!grown) return false;

    *points = grown;
    *cap = new_cap;
    return true;
}

static bool points_equal(fx_point a, fx_point b)
{
    return a.x == b.x && a.y == b.y;
}

static bool append_flat_point(fx_point **points,
                              size_t *count,
                              size_t *cap,
                              fx_point pt)
{
    if (*count && points_equal((*points)[*count - 1], pt)) return true;
    if (!ensure_temp_point_capacity(points, count, cap, 1)) return false;
    (*points)[(*count)++] = pt;
    return true;
}

static float point_line_distance_sq(fx_point p, fx_point a, fx_point b)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float denom = dx * dx + dy * dy;

    if (denom == 0.0f) {
        float px = p.x - a.x;
        float py = p.y - a.y;
        return px * px + py * py;
    }

    float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / denom;
    float qx = a.x + t * dx;
    float qy = a.y + t * dy;
    float ex = p.x - qx;
    float ey = p.y - qy;
    return ex * ex + ey * ey;
}

static bool flatten_quad_recursive(fx_point p0, fx_point p1, fx_point p2,
                                   float tol_sq, unsigned depth,
                                   fx_point **points,
                                   size_t *count, size_t *cap)
{
    fx_point p01;
    fx_point p12;
    fx_point p012;

    if (depth >= FX_FLATTEN_MAX_DEPTH ||
        point_line_distance_sq(p1, p0, p2) <= tol_sq) {
        return append_flat_point(points, count, cap, p2);
    }

    p01 = (fx_point){ (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
    p12 = (fx_point){ (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };
    p012 = (fx_point){ (p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f };

    return flatten_quad_recursive(p0, p01, p012, tol_sq, depth + 1,
                                  points, count, cap) &&
           flatten_quad_recursive(p012, p12, p2, tol_sq, depth + 1,
                                  points, count, cap);
}

static bool flatten_cubic_recursive(fx_point p0, fx_point p1,
                                    fx_point p2, fx_point p3,
                                    float tol_sq, unsigned depth,
                                    fx_point **points,
                                    size_t *count, size_t *cap)
{
    fx_point p01;
    fx_point p12;
    fx_point p23;
    fx_point p012;
    fx_point p123;
    fx_point p0123;

    if (depth >= FX_FLATTEN_MAX_DEPTH ||
        (point_line_distance_sq(p1, p0, p3) <= tol_sq &&
         point_line_distance_sq(p2, p0, p3) <= tol_sq)) {
        return append_flat_point(points, count, cap, p3);
    }

    p01 = (fx_point){ (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
    p12 = (fx_point){ (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };
    p23 = (fx_point){ (p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f };
    p012 = (fx_point){ (p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f };
    p123 = (fx_point){ (p12.x + p23.x) * 0.5f, (p12.y + p23.y) * 0.5f };
    p0123 = (fx_point){ (p012.x + p123.x) * 0.5f, (p012.y + p123.y) * 0.5f };

    return flatten_cubic_recursive(p0, p01, p012, p0123, tol_sq, depth + 1,
                                   points, count, cap) &&
           flatten_cubic_recursive(p0123, p123, p23, p3, tol_sq, depth + 1,
                                   points, count, cap);
}

static void update_bounds(fx_path *path, float x, float y)
{
    float min_x;
    float min_y;
    float max_x;
    float max_y;

    if (!path->has_bounds) {
        path->bounds = (fx_rect){ .x = x, .y = y, .w = 0.0f, .h = 0.0f };
        path->has_bounds = true;
        return;
    }

    min_x = path->bounds.x;
    min_y = path->bounds.y;
    max_x = path->bounds.x + path->bounds.w;
    max_y = path->bounds.y + path->bounds.h;

    if (x < min_x) min_x = x;
    if (y < min_y) min_y = y;
    if (x > max_x) max_x = x;
    if (y > max_y) max_y = y;

    path->bounds.x = min_x;
    path->bounds.y = min_y;
    path->bounds.w = max_x - min_x;
    path->bounds.h = max_y - min_y;
}

static bool push_verb_and_points(fx_path *path, uint8_t verb,
                                 const fx_point *points, size_t point_count)
{
    if (!path) return false;
    if (!ensure_verb_capacity(path, 1)) return false;
    if (point_count && !ensure_point_capacity(path, point_count)) return false;

    path->verbs[path->verb_count++] = verb;
    for (size_t i = 0; i < point_count; ++i) {
        path->points[path->point_count++] = points[i];
        update_bounds(path, points[i].x, points[i].y);
    }
    path->generation++;
    return true;
}

fx_path *fx_path_create(void)
{
    fx_path *path = calloc(1, sizeof(fx_path));
    if (path) {
        path->generation = 1;
    }
    return path;
}

void fx_path_destroy(fx_path *path)
{
    if (!path) return;
    free(path->verbs);
    free(path->points);
    free(path->fill_tris);
    free(path->stroke_tris);
    free(path);
}

void fx_path_reset(fx_path *path)
{
    if (!path) return;
    path->verb_count = 0;
    path->point_count = 0;
    path->bounds = (fx_rect){ 0 };
    path->has_bounds = false;
    path->generation++;
}

bool fx_path_move_to(fx_path *path, float x, float y)
{
    fx_point pt = { .x = x, .y = y };
    return push_verb_and_points(path, FX_PATH_MOVE, &pt, 1);
}

bool fx_path_line_to(fx_path *path, float x, float y)
{
    fx_point pt = { .x = x, .y = y };
    return push_verb_and_points(path, FX_PATH_LINE, &pt, 1);
}

bool fx_path_quad_to(fx_path *path, float cx, float cy, float x, float y)
{
    fx_point pts[2] = {
        { .x = cx, .y = cy },
        { .x = x,  .y = y  },
    };
    return push_verb_and_points(path, FX_PATH_QUAD, pts, 2);
}

bool fx_path_cubic_to(fx_path *path,
                      float cx0, float cy0,
                      float cx1, float cy1,
                      float x, float y)
{
    fx_point pts[3] = {
        { .x = cx0, .y = cy0 },
        { .x = cx1, .y = cy1 },
        { .x = x,   .y = y   },
    };
    return push_verb_and_points(path, FX_PATH_CUBIC, pts, 3);
}

bool fx_path_close(fx_path *path)
{
    return push_verb_and_points(path, FX_PATH_CLOSE, NULL, 0);
}

bool fx_path_add_rect(fx_path *path, const fx_rect *rect)
{
    float x0;
    float y0;
    float x1;
    float y1;

    if (!path || !rect) return false;
    x0 = rect->x;
    y0 = rect->y;
    x1 = rect->x + rect->w;
    y1 = rect->y + rect->h;

    return fx_path_move_to(path, x0, y0) &&
           fx_path_line_to(path, x1, y0) &&
           fx_path_line_to(path, x1, y1) &&
           fx_path_line_to(path, x0, y1) &&
           fx_path_close(path);
}

/*
 * Approximate an SVG elliptical arc as a sequence of cubic Bezier curves.
 * Follows the parameterisation in SVG 1.1 F.6.
 */
bool fx_path_arc_to(fx_path *path,
                    float rx, float ry,
                    float phi,
                    bool large_arc, bool sweep,
                    float x, float y)
{
    float x0, y0;
    float dx2, dy2;
    float cos_phi, sin_phi;
    float x1p, y1p;
    float lambda, s;
    float cxp, cyp, cx, cy;
    float theta1, delta_theta;
    int n_segments;
    float seg_delta;
    int i;

    if (!path) return false;

    if (path->point_count == 0)
        return fx_path_move_to(path, x, y);

    x0 = path->points[path->point_count - 1].x;
    y0 = path->points[path->point_count - 1].y;

    if (x0 == x && y0 == y)
        return true;

    rx = fabsf(rx);
    ry = fabsf(ry);

    if (rx == 0.0f || ry == 0.0f)
        return fx_path_line_to(path, x, y);

    cos_phi = cosf(phi);
    sin_phi = sinf(phi);

    dx2 = (x0 - x) * 0.5f;
    dy2 = (y0 - y) * 0.5f;

    x1p =  cos_phi * dx2 + sin_phi * dy2;
    y1p = -sin_phi * dx2 + cos_phi * dy2;

    lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0f) {
        float scale = sqrtf(lambda);
        rx *= scale;
        ry *= scale;
    }

    s = sqrtf(fmaxf(0.0f,
                    ((rx * rx * ry * ry) - (rx * rx * y1p * y1p) - (ry * ry * x1p * x1p)) /
                    ((rx * rx * y1p * y1p) + (ry * ry * x1p * x1p))));
    if (large_arc == sweep)
        s = -s;

    cxp =  s * rx * y1p / ry;
    cyp = -s * ry * x1p / rx;

    cx = cos_phi * cxp - sin_phi * cyp + (x0 + x) * 0.5f;
    cy = sin_phi * cxp + cos_phi * cyp + (y0 + y) * 0.5f;

    {
        float ux = ( x1p - cxp) / rx;
        float uy = ( y1p - cyp) / ry;
        float vx = (-x1p - cxp) / rx;
        float vy = (-y1p - cyp) / ry;

        theta1 = atan2f(uy, ux);
        delta_theta = atan2f(vy, vx) - atan2f(uy, ux);
    }

    if (!sweep && delta_theta > 0.0f)
        delta_theta -= 2.0f * M_PI;
    if ( sweep && delta_theta < 0.0f)
        delta_theta += 2.0f * M_PI;

    n_segments = (int)ceilf(fabsf(delta_theta) / (M_PI * 0.5f));
    if (n_segments < 1) n_segments = 1;
    seg_delta = delta_theta / (float)n_segments;

    for (i = 0; i < n_segments; ++i) {
        float a0 = theta1 + seg_delta * (float)i;
        float a1 = theta1 + seg_delta * (float)(i + 1);

        float d = (4.0f / 3.0f) * tanf((a1 - a0) * 0.25f);

        float ca0 = cosf(a0), sa0 = sinf(a0);
        float ca1 = cosf(a1), sa1 = sinf(a1);

        float p1x = ca0 - d * sa0, p1y = sa0 + d * ca0;
        float p2x = ca1 + d * sa1, p2y = sa1 - d * ca1;

        float ex0 = rx * p1x;
        float ey0 = ry * p1y;
        float cx0 = cos_phi * ex0 - sin_phi * ey0 + cx;
        float cy0 = sin_phi * ex0 + cos_phi * ey0 + cy;

        float ex1 = rx * p2x;
        float ey1 = ry * p2y;
        float cx1 = cos_phi * ex1 - sin_phi * ey1 + cx;
        float cy1 = sin_phi * ex1 + cos_phi * ey1 + cy;

        float ex2 = rx * ca1;
        float ey2 = ry * sa1;
        float px  = cos_phi * ex2 - sin_phi * ey2 + cx;
        float py  = sin_phi * ex2 + cos_phi * ey2 + cy;

        if (!fx_path_cubic_to(path, cx0, cy0, cx1, cy1, px, py))
            return false;
    }

    return true;
}

bool fx_path_get_bounds(const fx_path *path, fx_rect *out_bounds)
{
    if (!path || !path->has_bounds) return false;
    if (out_bounds) *out_bounds = path->bounds;
    return true;
}

size_t fx_path_verb_count(const fx_path *path)
{
    return path ? path->verb_count : 0;
}

size_t fx_path_point_count(const fx_path *path)
{
    return path ? path->point_count : 0;
}

bool fx_path_is_axis_aligned_rect(const fx_path *path, fx_rect *out_rect)
{
    fx_rect bounds;
    float min_x;
    float min_y;
    float max_x;
    float max_y;
    bool seen[4] = { false, false, false, false };

    if (!path) return false;
    if (path->verb_count != 5 || path->point_count != 4) return false;
    if (path->verbs[0] != FX_PATH_MOVE ||
        path->verbs[1] != FX_PATH_LINE ||
        path->verbs[2] != FX_PATH_LINE ||
        path->verbs[3] != FX_PATH_LINE ||
        path->verbs[4] != FX_PATH_CLOSE) {
        return false;
    }
    if (!fx_path_get_bounds(path, &bounds)) return false;

    min_x = bounds.x;
    min_y = bounds.y;
    max_x = bounds.x + bounds.w;
    max_y = bounds.y + bounds.h;
    if (bounds.w <= 0.0f || bounds.h <= 0.0f) return false;

    for (size_t i = 0; i < 4; ++i) {
        const fx_point *pt = &path->points[i];
        if (pt->x == min_x && pt->y == min_y) seen[0] = true;
        else if (pt->x == max_x && pt->y == min_y) seen[1] = true;
        else if (pt->x == max_x && pt->y == max_y) seen[2] = true;
        else if (pt->x == min_x && pt->y == max_y) seen[3] = true;
        else return false;
    }

    if (!(seen[0] && seen[1] && seen[2] && seen[3])) return false;
    if (out_rect) *out_rect = bounds;
    return true;
}

bool fx_path_get_line_loop(const fx_path *path,
                           const fx_point **out_points,
                           size_t *out_count)
{
    if (!path) return false;
    if (path->verb_count < 4 || path->point_count < 3) return false;
    if (path->verbs[0] != FX_PATH_MOVE) return false;
    if (path->verbs[path->verb_count - 1] != FX_PATH_CLOSE) return false;

    for (size_t i = 1; i + 1 < path->verb_count; ++i)
        if (path->verbs[i] != FX_PATH_LINE) return false;

    if (out_points) *out_points = path->points;
    if (out_count) *out_count = path->point_count;
    return true;
}

bool fx_path_flatten_polyline(const fx_path *path, float tolerance,
                              fx_arena *arena,
                              fx_point **out_points, size_t *out_count,
                              bool *out_closed)
{
    fx_point *flat = NULL;
    size_t flat_count = 0;
    size_t flat_cap = 0;
    fx_point current = { 0 };
    fx_point start = { 0 };
    size_t point_i = 0;
    bool have_current = false;
    float tol_sq;

    if (out_points) *out_points = NULL;
    if (out_count) *out_count = 0;
    if (out_closed) *out_closed = false;
    if (!path || !out_points || !out_count) return false;
    if (path->verb_count < 2 || path->point_count < 2) return false;
    if (tolerance <= 0.0f) tolerance = 0.25f;
    tol_sq = tolerance * tolerance;

    for (size_t i = 0; i < path->verb_count; ++i) {
        uint8_t verb = path->verbs[i];

        if (verb == FX_PATH_MOVE) {
            if (have_current || point_i >= path->point_count) goto fail;
            current = path->points[point_i++];
            start = current;
            have_current = true;
            if (!append_flat_point(&flat, &flat_count, &flat_cap, current))
                goto fail;
        } else if (verb == FX_PATH_LINE) {
            if (!have_current || point_i >= path->point_count) goto fail;
            current = path->points[point_i++];
            if (!append_flat_point(&flat, &flat_count, &flat_cap, current))
                goto fail;
        } else if (verb == FX_PATH_QUAD) {
            fx_point c;
            fx_point p;
            if (!have_current || point_i + 1 >= path->point_count) goto fail;
            c = path->points[point_i++];
            p = path->points[point_i++];
            if (!flatten_quad_recursive(current, c, p, tol_sq, 0,
                                        &flat, &flat_count, &flat_cap)) {
                goto fail;
            }
            current = p;
        } else if (verb == FX_PATH_CUBIC) {
            fx_point c0;
            fx_point c1;
            fx_point p;
            if (!have_current || point_i + 2 >= path->point_count) goto fail;
            c0 = path->points[point_i++];
            c1 = path->points[point_i++];
            p = path->points[point_i++];
            if (!flatten_cubic_recursive(current, c0, c1, p, tol_sq, 0,
                                         &flat, &flat_count, &flat_cap)) {
                goto fail;
            }
            current = p;
        } else if (verb == FX_PATH_CLOSE) {
            if (!have_current || i != path->verb_count - 1) goto fail;
            if (!points_equal(current, start) && flat_count >= 2 &&
                points_equal(flat[flat_count - 1], start)) {
                flat_count--;
            }
            if (out_closed) *out_closed = true;
        } else {
            goto fail;
        }
    }

    if (point_i != path->point_count || flat_count < 2) goto fail;
    if (flat_count >= 2 && points_equal(flat[flat_count - 1], flat[0]))
        flat_count--;
    if ((out_closed && *out_closed && flat_count < 3) || flat_count < 2)
        goto fail;

    /* Copy to arena */
    *out_points = fx_arena_alloc(arena, flat_count * sizeof(fx_point));
    if (!*out_points) goto fail;
    memcpy(*out_points, flat, flat_count * sizeof(fx_point));
    *out_count = flat_count;

    free(flat);
    return true;

fail:
    free(flat);
    return false;
}

bool fx_path_flatten_line_loop(const fx_path *path, float tolerance,
                               fx_arena *arena,
                               fx_point **out_points, size_t *out_count)
{
    bool closed = false;

    if (!fx_path_flatten_polyline(path, tolerance, arena, out_points, out_count, &closed))
        return false;
    if (!closed) {
        /* Points were allocated in arena, so we can't 'free' them, but they'll be reclaimed with arena reset */
        *out_points = NULL;
        *out_count = 0;
        return false;
    }
    return true;
}

/* ---------- matrix & path transform ---------- */

void fx_matrix_multiply(fx_matrix *out, const fx_matrix *a, const fx_matrix *b)
{
    float a0 = a->m[0], a1 = a->m[1], a2 = a->m[2], a3 = a->m[3], a4 = a->m[4], a5 = a->m[5];
    float b0 = b->m[0], b1 = b->m[1], b2 = b->m[2], b3 = b->m[3], b4 = b->m[4], b5 = b->m[5];

    out->m[0] = a0 * b0 + a2 * b1;
    out->m[1] = a1 * b0 + a3 * b1;
    out->m[2] = a0 * b2 + a2 * b3;
    out->m[3] = a1 * b2 + a3 * b3;
    out->m[4] = a0 * b4 + a2 * b5 + a4;
    out->m[5] = a1 * b4 + a3 * b5 + a5;
}

void fx_matrix_transform_point(const fx_matrix *m, float *x, float *y)
{
    float px = *x;
    float py = *y;
    *x = m->m[0] * px + m->m[2] * py + m->m[4];
    *y = m->m[1] * px + m->m[3] * py + m->m[5];
}

fx_path *fx_path_transform(const fx_path *src, const fx_matrix *m)
{
    if (!src || !m) return NULL;

    fx_path *dst = fx_path_create();
    if (!dst) return NULL;

    if (!ensure_verb_capacity(dst, src->verb_count) ||
        !ensure_point_capacity(dst, src->point_count)) {
        fx_path_destroy(dst);
        return NULL;
    }

    memcpy(dst->verbs, src->verbs, src->verb_count);
    dst->verb_count = src->verb_count;

    for (size_t i = 0; i < src->point_count; ++i) {
        float x = src->points[i].x;
        float y = src->points[i].y;
        fx_matrix_transform_point(m, &x, &y);
        dst->points[i] = (fx_point){ x, y };
        update_bounds(dst, x, y);
    }
    dst->point_count = src->point_count;

    return dst;
}
