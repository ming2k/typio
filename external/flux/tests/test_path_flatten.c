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

static bool test_flatten_quad_loop(void)
{
    fx_path *path = fx_path_create();
    fx_point *points = NULL;
    size_t count = 0;
    fx_arena arena;
    fx_arena_init(&arena, 0);

    CHECK(path != NULL);
    CHECK(fx_path_move_to(path, 0.0f, 40.0f));
    CHECK(fx_path_quad_to(path, 0.0f, 0.0f, 40.0f, 0.0f));
    CHECK(fx_path_line_to(path, 80.0f, 40.0f));
    CHECK(fx_path_line_to(path, 40.0f, 80.0f));
    CHECK(fx_path_close(path));

    CHECK(fx_path_flatten_line_loop(path, 0.25f, &arena, &points, &count));
    CHECK(points != NULL);
    CHECK(count > 4);
    CHECK(points[0].x == 0.0f && points[0].y == 40.0f);
    CHECK(points[count - 1].x == 40.0f && points[count - 1].y == 80.0f);

    fx_arena_destroy(&arena);
    fx_path_destroy(path);
    return true;
}

static bool test_flatten_cubic_loop(void)
{
    fx_path *path = fx_path_create();
    fx_point *points = NULL;
    size_t count = 0;
    fx_arena arena;
    fx_arena_init(&arena, 0);

    CHECK(path != NULL);
    CHECK(fx_path_move_to(path, 20.0f, 70.0f));
    CHECK(fx_path_cubic_to(path, 20.0f, 20.0f, 60.0f, 20.0f, 60.0f, 70.0f));
    CHECK(fx_path_line_to(path, 40.0f, 90.0f));
    CHECK(fx_path_close(path));

    CHECK(fx_path_flatten_line_loop(path, 0.25f, &arena, &points, &count));
    CHECK(points != NULL);
    CHECK(count > 4);
    CHECK(points[0].x == 20.0f && points[0].y == 70.0f);
    CHECK(points[count - 1].x == 40.0f && points[count - 1].y == 90.0f);

    fx_arena_destroy(&arena);
    fx_path_destroy(path);
    return true;
}

static bool test_reject_open_path(void)
{
    fx_path *path = fx_path_create();
    fx_point *points = NULL;
    size_t count = 0;
    bool closed = true;
    fx_arena arena;
    fx_arena_init(&arena, 0);

    CHECK(path != NULL);
    CHECK(fx_path_move_to(path, 0.0f, 0.0f));
    CHECK(fx_path_line_to(path, 10.0f, 0.0f));
    CHECK(fx_path_line_to(path, 10.0f, 10.0f));
    CHECK(fx_path_flatten_polyline(path, 0.25f, &arena, &points, &count, &closed));
    CHECK(points != NULL);
    CHECK(count == 3);
    CHECK(!closed);

    points = NULL;
    count = 0;
    CHECK(!fx_path_flatten_line_loop(path, 0.25f, &arena, &points, &count));
    CHECK(points == NULL);
    CHECK(count == 0);

    fx_arena_destroy(&arena);
    fx_path_destroy(path);
    return true;
}

int main(void)
{
    if (!test_flatten_quad_loop()) return 1;
    if (!test_flatten_cubic_loop()) return 1;
    if (!test_reject_open_path()) return 1;
    return 0;
}
