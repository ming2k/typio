#include "internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
                    #cond, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

/* Approximate float equality for matrix entries */
#define CHECK_FEQ(a, b) CHECK(fabsf((a) - (b)) < 1e-5f)

static bool test_save_restore_identity(void)
{
    fx_surface surface = { 0 };
    surface.canvas.owner = &surface;
    fx_matrix_identity(&surface.canvas.current_matrix);

    fx_matrix m;
    fx_get_matrix(&surface.canvas, &m);
    CHECK(fx_matrix_is_identity(&m));

    fx_save(&surface.canvas);
    fx_translate(&surface.canvas, 10.0f, 20.0f);
    fx_get_matrix(&surface.canvas, &m);
    CHECK_FEQ(m.m[4], 10.0f);
    CHECK_FEQ(m.m[5], 20.0f);

    fx_restore(&surface.canvas);
    fx_get_matrix(&surface.canvas, &m);
    CHECK(fx_matrix_is_identity(&m));

    free(surface.canvas.state_stack);
    return true;
}

static bool test_nested_save_restore(void)
{
    fx_surface surface = { 0 };
    surface.canvas.owner = &surface;
    fx_matrix_identity(&surface.canvas.current_matrix);
    fx_matrix m;

    fx_save(&surface.canvas);          /* L0: identity */
    fx_translate(&surface.canvas, 1.0f, 0.0f);
    fx_save(&surface.canvas);          /* L1: translate(1,0) */
    fx_scale(&surface.canvas, 2.0f, 2.0f);
    fx_save(&surface.canvas);          /* L2: translate(1,0) * scale(2,2) */
    fx_rotate(&surface.canvas, 0.0f);  /* no-op, but uses concat path */

    fx_get_matrix(&surface.canvas, &m);
    CHECK_FEQ(m.m[0], 2.0f);
    CHECK_FEQ(m.m[3], 2.0f);
    CHECK_FEQ(m.m[4], 1.0f);
    CHECK_FEQ(m.m[5], 0.0f);

    fx_restore(&surface.canvas);       /* pop S2 -> back to T*S */
    fx_get_matrix(&surface.canvas, &m);
    CHECK_FEQ(m.m[0], 2.0f);
    CHECK_FEQ(m.m[3], 2.0f);
    CHECK_FEQ(m.m[4], 1.0f);
    CHECK_FEQ(m.m[5], 0.0f);

    fx_restore(&surface.canvas);       /* pop S1 -> back to T(1,0) */
    fx_get_matrix(&surface.canvas, &m);
    CHECK_FEQ(m.m[0], 1.0f);
    CHECK_FEQ(m.m[3], 1.0f);
    CHECK_FEQ(m.m[4], 1.0f);
    CHECK_FEQ(m.m[5], 0.0f);

    fx_restore(&surface.canvas);       /* pop S0 -> back to I */
    fx_get_matrix(&surface.canvas, &m);
    CHECK_FEQ(m.m[0], 1.0f);
    CHECK_FEQ(m.m[3], 1.0f);
    CHECK_FEQ(m.m[4], 0.0f);
    CHECK_FEQ(m.m[5], 0.0f);

    fx_restore(&surface.canvas);       /* stack empty, no-op */
    fx_get_matrix(&surface.canvas, &m);
    CHECK_FEQ(m.m[4], 0.0f);
    CHECK_FEQ(m.m[5], 0.0f);

    free(surface.canvas.state_stack);
    return true;
}

static bool test_set_matrix_and_concat(void)
{
    fx_surface surface = { 0 };
    surface.canvas.owner = &surface;
    fx_matrix_identity(&surface.canvas.current_matrix);
    fx_matrix m;

    fx_matrix custom = {
        .m = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f }
    };
    fx_set_matrix(&surface.canvas, &custom);
    fx_get_matrix(&surface.canvas, &m);
    CHECK(memcmp(&m, &custom, sizeof(m)) == 0);

    fx_matrix trans = {
        .m = { 1.0f, 0.0f, 0.0f, 1.0f, 10.0f, 20.0f }
    };
    fx_concat(&surface.canvas, &trans);
    fx_get_matrix(&surface.canvas, &m);
    /* m = custom * trans  (post-multiplication) */
    /* [1 3; 2 4] * [1 0; 0 1] = [1 3; 2 4] */
    /* tx = 1*10 + 3*20 + 5 = 75, ty = 2*10 + 4*20 + 6 = 106 */
    CHECK_FEQ(m.m[0], 1.0f);
    CHECK_FEQ(m.m[1], 2.0f);
    CHECK_FEQ(m.m[2], 3.0f);
    CHECK_FEQ(m.m[3], 4.0f);
    CHECK_FEQ(m.m[4], 75.0f);
    CHECK_FEQ(m.m[5], 106.0f);

    free(surface.canvas.state_stack);
    return true;
}

static bool test_rotate_90_degrees(void)
{
    fx_surface surface = { 0 };
    surface.canvas.owner = &surface;
    fx_matrix_identity(&surface.canvas.current_matrix);
    fx_matrix m;

    fx_rotate(&surface.canvas, 3.14159265f / 2.0f); /* 90° */
    fx_get_matrix(&surface.canvas, &m);
    CHECK_FEQ(m.m[0], 0.0f);
    CHECK_FEQ(m.m[1], 1.0f);
    CHECK_FEQ(m.m[2], -1.0f);
    CHECK_FEQ(m.m[3], 0.0f);

    free(surface.canvas.state_stack);
    return true;
}

static bool test_null_safety(void)
{
    /* All transform functions must be safe with NULL canvas */
    fx_save(NULL);
    fx_restore(NULL);
    fx_translate(NULL, 1.0f, 2.0f);
    fx_scale(NULL, 1.0f, 2.0f);
    fx_rotate(NULL, 1.0f);
    fx_concat(NULL, &(fx_matrix){0});
    fx_set_matrix(NULL, &(fx_matrix){0});

    fx_matrix m;
    fx_get_matrix(NULL, &m);
    /* Should not crash; matrix untouched if canvas is NULL */
    return true;
}

int main(void)
{
    if (!test_save_restore_identity()) return 1;
    if (!test_nested_save_restore()) return 1;
    if (!test_set_matrix_and_concat()) return 1;
    if (!test_rotate_90_degrees()) return 1;
    if (!test_null_safety()) return 1;
    printf("transform_stack OK\n");
    return 0;
}
