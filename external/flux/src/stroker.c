#include "internal.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static bool append_triangle(fx_point *tris, size_t *count,
                            fx_point a, fx_point b, fx_point c)
{
    tris[(*count)++] = a;
    tris[(*count)++] = b;
    tris[(*count)++] = c;
    return true;
}

static fx_point normalize(fx_point v)
{
    float len = sqrtf(v.x * v.x + v.y * v.y);
    if (len > 0.0f) return (fx_point){ v.x / len, v.y / len };
    return (fx_point){ 0, 0 };
}

static bool get_normal(fx_point a, fx_point b, fx_point *out_n)
{
    fx_point d = { b.x - a.x, b.y - a.y };
    fx_point n = { -d.y, d.x };
    fx_point unit_n = normalize(n);
    if (unit_n.x == 0.0f && unit_n.y == 0.0f) return false;
    *out_n = unit_n;
    return true;
}

static bool append_join(fx_point *tris, size_t *count,
                        fx_point p, fx_point n1, fx_point n2,
                        const fx_paint *paint)
{
    float cross = n1.x * n2.y - n1.y * n2.x;
    if (fabsf(cross) < 1e-4f) return true; /* Collinear or nearly so */

    bool left = cross > 0.0f;
    float half_w = paint->stroke_width * 0.5f;

    if (paint->line_join == FX_JOIN_MITER) {
        float dot = n1.x * n2.x + n1.y * n2.y;
        fx_point bisect = normalize((fx_point){ n1.x + n2.x, n1.y + n2.y });
        float cos_half_theta = sqrtf((1.0f + dot) * 0.5f);
        if (cos_half_theta > 1e-4f) {
            float miter_dist = half_w / cos_half_theta;
            if (miter_dist <= paint->miter_limit * half_w) {
                fx_point miter_pt = { p.x + bisect.x * miter_dist, p.y + bisect.y * miter_dist };
                if (left) {
                    miter_pt.x = p.x - bisect.x * miter_dist;
                    miter_pt.y = p.y - bisect.y * miter_dist;
                    append_triangle(tris, count, p, 
                                    (fx_point){ p.x - n1.x * half_w, p.y - n1.y * half_w },
                                    miter_pt);
                    append_triangle(tris, count, p,
                                    miter_pt,
                                    (fx_point){ p.x - n2.x * half_w, p.y - n2.y * half_w });
                } else {
                    append_triangle(tris, count, p, 
                                    (fx_point){ p.x + n1.x * half_w, p.y + n1.y * half_w },
                                    miter_pt);
                    append_triangle(tris, count, p,
                                    miter_pt,
                                    (fx_point){ p.x + n2.x * half_w, p.y + n2.y * half_w });
                }
                return true;
            }
        }
    }

    if (paint->line_join == FX_JOIN_ROUND) {
        int steps = 8;
        fx_point v1 = { (left ? -n1.x : n1.x) * half_w, (left ? -n1.y : n1.y) * half_w };
        fx_point v2 = { (left ? -n2.x : n2.x) * half_w, (left ? -n2.y : n2.y) * half_w };
        float a1 = atan2f(v1.y, v1.x);
        float a2 = atan2f(v2.y, v2.x);
        
        float diff = a2 - a1;
        if (left) {
            while (diff > 0) diff -= 2.0f * M_PI;
            while (diff < -2.0f * M_PI) diff += 2.0f * M_PI;
        } else {
            while (diff < 0) diff += 2.0f * M_PI;
            while (diff > 2.0f * M_PI) diff -= 2.0f * M_PI;
        }

        fx_point prev = { p.x + v1.x, p.y + v1.y };
        for (int i = 1; i <= steps; ++i) {
            float a = a1 + diff * (float)i / (float)steps;
            fx_point next = { p.x + cosf(a) * half_w, p.y + sinf(a) * half_w };
            append_triangle(tris, count, p, prev, next);
            prev = next;
        }
        return true;
    }

    /* Bevel join */
    if (left) {
        append_triangle(tris, count, p,
                        (fx_point){ p.x - n1.x * half_w, p.y - n1.y * half_w },
                        (fx_point){ p.x - n2.x * half_w, p.y - n2.y * half_w });
    } else {
        append_triangle(tris, count, p,
                        (fx_point){ p.x + n1.x * half_w, p.y + n1.y * half_w },
                        (fx_point){ p.x + n2.x * half_w, p.y + n2.y * half_w });
    }
    return true;
}

bool fx_stroke_polyline(const fx_point *points, size_t count, bool closed,
                        const fx_paint *paint, fx_arena *arena, fx_point **out_tris, size_t *out_count)
{
    if (!points || !out_tris || !out_count || count < 2 || !paint || paint->stroke_width <= 0.0f)
        return false;

    float half_w = paint->stroke_width * 0.5f;
    int steps = 8;
    
    /* Estimate max triangle count */
    size_t max_tris = (count + 1) * 2; /* segments */
    max_tris += count * steps; /* joins */
    max_tris += 2 * steps; /* caps */
    
    fx_point *tris = fx_arena_alloc(arena, max_tris * 3 * sizeof(fx_point));
    if (!tris) return false;
    size_t tri_count = 0;

    fx_point *normals = fx_arena_alloc(arena, count * sizeof(fx_point));
    if (!normals) return false;

    size_t seg_count = closed ? count : count - 1;
    for (size_t i = 0; i < seg_count; ++i) {
        if (!get_normal(points[i], points[(i + 1) % count], &normals[i])) {
            normals[i] = (i > 0) ? normals[i-1] : (fx_point){0, 0};
        }
    }

    for (size_t i = 0; i < seg_count; ++i) {
        size_t j = (i + 1) % count;
        fx_point n = normals[i];
        fx_point p1 = points[i];
        fx_point p2 = points[j];

        fx_point v1l = { p1.x + n.x * half_w, p1.y + n.y * half_w };
        fx_point v1r = { p1.x - n.x * half_w, p1.y - n.y * half_w };
        fx_point v2l = { p2.x + n.x * half_w, p2.y + n.y * half_w };
        fx_point v2r = { p2.x - n.x * half_w, p2.y - n.y * half_w };

        append_triangle(tris, &tri_count, v1l, v1r, v2r);
        append_triangle(tris, &tri_count, v1l, v2r, v2l);
    }

    for (size_t i = 0; i < count; ++i) {
        if (!closed && (i == 0 || i == count - 1)) continue;
        size_t prev_i = (i + count - 1) % count;
        size_t next_i = i;
        append_join(tris, &tri_count, points[i], normals[prev_i], normals[next_i], paint);
    }

    if (!closed) {
        /* Start cap */
        fx_point p0 = points[0];
        fx_point n0 = normals[0];
        fx_point d0 = normalize((fx_point){ points[1].x - p0.x, points[1].y - p0.y });

        if (paint->line_cap == FX_CAP_SQUARE) {
            fx_point ext = { -d0.x * half_w, -d0.y * half_w };
            fx_point v0l = { p0.x + n0.x * half_w, p0.y + n0.y * half_w };
            fx_point v0r = { p0.x - n0.x * half_w, p0.y - n0.y * half_w };
            fx_point e0l = { v0l.x + ext.x, v0l.y + ext.y };
            fx_point e0r = { v0r.x + ext.x, v0r.y + ext.y };
            append_triangle(tris, &tri_count, v0l, v0r, e0r);
            append_triangle(tris, &tri_count, v0l, e0r, e0l);
        } else if (paint->line_cap == FX_CAP_ROUND) {
            fx_point v_start = { n0.x * half_w, n0.y * half_w };
            float angle_start = atan2f(v_start.y, v_start.x);
            fx_point prev = { p0.x + v_start.x, p0.y + v_start.y };
            for (int k = 1; k <= steps; ++k) {
                float a = angle_start + (float)k / (float)steps * M_PI;
                fx_point next = { p0.x + cosf(a) * half_w, p0.y + sinf(a) * half_w };
                append_triangle(tris, &tri_count, p0, prev, next);
                prev = next;
            }
        }

        /* End cap */
        size_t last = count - 1;
        fx_point pl = points[last];
        fx_point nl = normals[last - 1];
        fx_point dl = normalize((fx_point){ pl.x - points[last-1].x, pl.y - points[last-1].y });

        if (paint->line_cap == FX_CAP_SQUARE) {
            fx_point ext = { dl.x * half_w, dl.y * half_w };
            fx_point vll = { pl.x + nl.x * half_w, pl.y + nl.y * half_w };
            fx_point vlr = { pl.x - nl.x * half_w, pl.y - nl.y * half_w };
            fx_point ell = { vll.x + ext.x, vll.y + ext.y };
            fx_point elr = { vlr.x + ext.x, vlr.y + ext.y };
            append_triangle(tris, &tri_count, vll, vlr, elr);
            append_triangle(tris, &tri_count, vll, elr, ell);
        } else if (paint->line_cap == FX_CAP_ROUND) {
            fx_point v_start = { -nl.x * half_w, -nl.y * half_w };
            float angle_start = atan2f(v_start.y, v_start.x);
            fx_point prev = { pl.x + v_start.x, pl.y + v_start.y };
            for (int k = 1; k <= steps; ++k) {
                float a = angle_start + (float)k / (float)steps * M_PI;
                fx_point next = { pl.x + cosf(a) * half_w, pl.y + sinf(a) * half_w };
                append_triangle(tris, &tri_count, pl, prev, next);
                prev = next;
            }
        }
    }

    *out_tris = tris;
    *out_count = tri_count;
    return true;
}
