#include "internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
                    #cond, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#define EPS 1.0f

static bool test_arc_quarter(void)
{
    fx_path *path = fx_path_create();
    fx_point *points = NULL;
    size_t count = 0;
    fx_arena arena;
    fx_arena_init(&arena, 0);

    CHECK(path != NULL);
    CHECK(fx_path_move_to(path, 100.0f, 0.0f));
    CHECK(fx_path_arc_to(path, 100.0f, 100.0f, 0.0f, false, false, 0.0f, 100.0f));
    CHECK(fx_path_close(path));

    CHECK(fx_path_flatten_line_loop(path, 0.25f, &arena, &points, &count));
    CHECK(points != NULL);
    CHECK(count >= 4);

    /* End point should be near (0, 100) */
    CHECK(fabsf(points[count - 1].x - 0.0f) < EPS);
    CHECK(fabsf(points[count - 1].y - 100.0f) < EPS);

    fx_arena_destroy(&arena);
    fx_path_destroy(path);
    return true;
}

static bool test_arc_full_circle(void)
{
    fx_path *path = fx_path_create();
    fx_point *points = NULL;
    size_t count = 0;
    fx_arena arena;
    fx_arena_init(&arena, 0);

    CHECK(path != NULL);
    CHECK(fx_path_move_to(path, 50.0f, 0.0f));
    /* Two 180-degree arcs make a full circle */
    CHECK(fx_path_arc_to(path, 50.0f, 50.0f, 0.0f, false, false, -50.0f, 0.0f));
    CHECK(fx_path_arc_to(path, 50.0f, 50.0f, 0.0f, false, false, 50.0f, 0.0f));
    CHECK(fx_path_close(path));

    CHECK(fx_path_flatten_line_loop(path, 0.25f, &arena, &points, &count));
    CHECK(points != NULL);
    CHECK(count >= 8);

    /* End point should loop back to start */
    CHECK(fabsf(points[count - 1].x - 50.0f) < EPS);
    CHECK(fabsf(points[count - 1].y - 0.0f) < EPS);

    fx_arena_destroy(&arena);
    fx_path_destroy(path);
    return true;
}

static bool test_arc_degenerate(void)
{
    fx_path *path = fx_path_create();

    CHECK(path != NULL);
    CHECK(fx_path_move_to(path, 10.0f, 10.0f));
    /* rx = 0 should degenerate to a line */
    CHECK(fx_path_arc_to(path, 0.0f, 50.0f, 0.0f, false, false, 60.0f, 10.0f));
    CHECK(fx_path_verb_count(path) == 2); /* move + line */

    fx_path_destroy(path);
    return true;
}

static bool test_arc_zero_length(void)
{
    fx_path *path = fx_path_create();

    CHECK(path != NULL);
    CHECK(fx_path_move_to(path, 10.0f, 10.0f));
    /* start == end should be a no-op */
    CHECK(fx_path_arc_to(path, 50.0f, 50.0f, 0.0f, false, false, 10.0f, 10.0f));
    CHECK(fx_path_verb_count(path) == 1); /* only move */

    fx_path_destroy(path);
    return true;
}

int main(void)
{
    if (!test_arc_quarter()) return 1;
    if (!test_arc_full_circle()) return 1;
    if (!test_arc_degenerate()) return 1;
    if (!test_arc_zero_length()) return 1;
    return 0;
}
