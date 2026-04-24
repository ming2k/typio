#include "internal.h"
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

static bool test_convex_polygon(void)
{
    fx_point poly[4] = {
        { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 }
    };
    fx_point *tris = NULL;
    size_t count = 0;
    fx_arena arena;
    fx_arena_init(&arena, 0);

    CHECK(fx_tessellate_simple_polygon(poly, 4, &arena, &tris, &count));
    CHECK(count == 6); /* 2 triangles */
    
    fx_arena_destroy(&arena);
    return true;
}

static bool test_concave_polygon(void)
{
    fx_point poly[5] = {
        { 0, 0 }, { 10, 0 }, { 10, 10 }, { 5, 5 }, { 0, 10 }
    };
    fx_point *tris = NULL;
    size_t count = 0;
    fx_arena arena;
    fx_arena_init(&arena, 0);

    CHECK(fx_tessellate_simple_polygon(poly, 5, &arena, &tris, &count));
    CHECK(count == 9); /* 3 triangles */
    
    fx_arena_destroy(&arena);
    return true;
}

static bool test_reject_degenerate(void)
{
    fx_point poly[3] = {
        { 0, 0 }, { 10, 0 }, { 20, 0 }
    };
    fx_point *tris = NULL;
    size_t count = 0;
    fx_arena arena;
    fx_arena_init(&arena, 0);

    CHECK(!fx_tessellate_simple_polygon(poly, 3, &arena, &tris, &count));
    
    fx_arena_destroy(&arena);
    return true;
}

int main(void)
{
    if (!test_convex_polygon()) return 1;
    if (!test_concave_polygon()) return 1;
    if (!test_reject_degenerate()) return 1;
    return 0;
}
