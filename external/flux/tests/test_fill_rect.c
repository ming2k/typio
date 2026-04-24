#include "internal.h"
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

static bool test_fill_rect_records_path_op(void)
{
    fx_surface surface = { 0 };
    surface.canvas.owner = &surface;
    fx_matrix_identity(&surface.canvas.current_matrix);

    fx_color red = fx_color_rgba(255, 0, 0, 255);
    CHECK(fx_fill_rect(&surface.canvas, &(fx_rect){ 10.0f, 20.0f, 30.0f, 40.0f }, red));
    CHECK(fx_canvas_op_count(&surface.canvas) == 1);

    fx_op *op = &surface.canvas.ops[0];
    CHECK(op->kind == FX_OP_FILL_PATH);
    CHECK(op->u.fill_path.owns_path);
    CHECK(op->u.fill_path.paint.color == red);

    fx_path *path = (fx_path *)op->u.fill_path.path;
    CHECK(path != NULL);
    CHECK(fx_path_verb_count(path) == 5);   /* move, line, line, line, close */
    CHECK(fx_path_point_count(path) == 4);

    fx_rect bounds = { 0 };
    CHECK(fx_path_get_bounds(path, &bounds));
    CHECK(bounds.x == 10.0f);
    CHECK(bounds.y == 20.0f);
    CHECK(bounds.w == 30.0f);
    CHECK(bounds.h == 40.0f);

    surface.canvas.op_count = 0;
    surface.canvas.has_clear = false;
    surface.canvas.clear_color = 0;
    free(surface.canvas.ops);
    surface.canvas.ops = NULL;
    surface.canvas.op_cap = 0;
    return true;
}

static bool test_fill_rect_with_transform(void)
{
    fx_surface surface = { 0 };
    surface.canvas.owner = &surface;
    fx_matrix_identity(&surface.canvas.current_matrix);

    fx_translate(&surface.canvas, 5.0f, 10.0f);
    fx_color blue = fx_color_rgba(0, 0, 255, 255);
    CHECK(fx_fill_rect(&surface.canvas, &(fx_rect){ 0.0f, 0.0f, 10.0f, 20.0f }, blue));

    fx_op *op = &surface.canvas.ops[0];
    CHECK(op->kind == FX_OP_FILL_PATH);
    CHECK(op->u.fill_path.owns_path);

    fx_path *path = (fx_path *)op->u.fill_path.path;
    fx_rect bounds = { 0 };
    CHECK(fx_path_get_bounds(path, &bounds));
    CHECK(bounds.x == 5.0f);
    CHECK(bounds.y == 10.0f);
    CHECK(bounds.w == 10.0f);
    CHECK(bounds.h == 20.0f);

    surface.canvas.op_count = 0;
    surface.canvas.has_clear = false;
    surface.canvas.clear_color = 0;
    free(surface.canvas.ops);
    surface.canvas.ops = NULL;
    surface.canvas.op_cap = 0;
    free(surface.canvas.state_stack);
    surface.canvas.state_stack = NULL;
    return true;
}

static bool test_fill_rect_null_safety(void)
{
    fx_color green = fx_color_rgba(0, 255, 0, 255);
    CHECK(!fx_fill_rect(NULL, &(fx_rect){0, 0, 1, 1}, green));
    CHECK(!fx_fill_rect(&(fx_canvas){0}, NULL, green));
    return true;
}

int main(void)
{
    if (!test_fill_rect_records_path_op()) return 1;
    if (!test_fill_rect_with_transform()) return 1;
    if (!test_fill_rect_null_safety()) return 1;
    printf("fill_rect OK\n");
    return 0;
}
