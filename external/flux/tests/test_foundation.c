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

static bool test_path_object(void)
{
    fx_path *path = fx_path_create();
    fx_rect bounds = { 0 };

    CHECK(path != NULL);
    CHECK(fx_path_add_rect(path, &(fx_rect){ 10.0f, 20.0f, 30.0f, 40.0f }));
    CHECK(fx_path_verb_count(path) == 5);
    CHECK(fx_path_point_count(path) == 4);
    CHECK(fx_path_get_bounds(path, &bounds));
    CHECK(bounds.x == 10.0f);
    CHECK(bounds.y == 20.0f);
    CHECK(bounds.w == 30.0f);
    CHECK(bounds.h == 40.0f);

    fx_path_reset(path);
    CHECK(fx_path_verb_count(path) == 0);
    CHECK(fx_path_point_count(path) == 0);
    CHECK(!fx_path_get_bounds(path, &bounds));

    CHECK(fx_path_move_to(path, 1.0f, 2.0f));
    CHECK(fx_path_line_to(path, 5.0f, 2.0f));
    CHECK(fx_path_line_to(path, 6.0f, 5.0f));
    CHECK(fx_path_line_to(path, 2.0f, 6.0f));
    CHECK(fx_path_close(path));
    CHECK(fx_path_verb_count(path) == 5);
    CHECK(fx_path_point_count(path) == 4);
    CHECK(fx_path_get_bounds(path, &bounds));
    CHECK(bounds.x == 1.0f);
    CHECK(bounds.y == 2.0f);
    CHECK(bounds.w == 5.0f);
    CHECK(bounds.h == 4.0f);

    fx_path_reset(path);
    CHECK(fx_path_move_to(path, 1.0f, 2.0f));
    CHECK(fx_path_quad_to(path, 3.0f, 4.0f, 5.0f, 6.0f));
    CHECK(fx_path_cubic_to(path, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f));
    CHECK(fx_path_close(path));
    CHECK(fx_path_verb_count(path) == 4);
    CHECK(fx_path_point_count(path) == 6);
    CHECK(fx_path_get_bounds(path, &bounds));
    CHECK(bounds.x == 1.0f);
    CHECK(bounds.y == 2.0f);
    CHECK(bounds.w == 10.0f);
    CHECK(bounds.h == 10.0f);

    fx_path_destroy(path);
    return true;
}

static bool test_image_object(void)
{
    fx_context fake_ctx = { 0 };
    uint32_t pixels[4] = {
        0x11223344u, 0x55667788u,
        0x99AABBCCu, 0xDDEEFF00u,
    };
    fx_image_desc out_desc = { 0 };
    size_t size = 0;
    size_t stride = 0;
    const uint32_t *copied_pixels;
    fx_image *image = fx_image_create(&fake_ctx, &(fx_image_desc){
        .width = 2,
        .height = 2,
        .format = FX_FMT_RGBA8_UNORM,
        .data = pixels,
    });

    CHECK(image != NULL);
    CHECK(fx_image_get_desc(image, &out_desc));
    CHECK(out_desc.width == 2);
    CHECK(out_desc.height == 2);
    CHECK(out_desc.format == FX_FMT_RGBA8_UNORM);
    CHECK(out_desc.stride == 8);
    CHECK(out_desc.data == NULL);

    copied_pixels = fx_image_data(image, &size, &stride);
    CHECK(copied_pixels != NULL);
    CHECK(size == sizeof(pixels));
    CHECK(stride == 8);
    CHECK(memcmp(copied_pixels, pixels, sizeof(pixels)) == 0);

    pixels[0] = 0;
    CHECK(copied_pixels[0] == 0x11223344u);

    fx_image_destroy(image);
    return true;
}

static bool test_font_and_glyph_run(void)
{
    fx_context fake_ctx = { 0 };
    FT_Init_FreeType(&fake_ctx.ft_lib);

    fx_font_desc desc = {
        .family = "Noto Sans",
        .source_name = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        .size = 16.0f,
    };
    fx_font_desc out_desc = { 0 };
    const fx_glyph *glyphs;
    fx_font *font = fx_font_create(&fake_ctx, &desc);
    if (!font) {
        /* Fallback if DejaVuSans is missing */
        FT_Done_FreeType(fake_ctx.ft_lib);
        return true;
    }
    fx_glyph_run *run = fx_glyph_run_create(1);

    CHECK(font != NULL);
    CHECK(fx_font_get_desc(font, &out_desc));
    CHECK(strcmp(out_desc.family, "Noto Sans") == 0);
    CHECK(out_desc.size == 16.0f);


    CHECK(run != NULL);
    CHECK(fx_glyph_run_count(run) == 0);
    CHECK(fx_glyph_run_append(run, 17, 1.5f, 2.5f));
    CHECK(fx_glyph_run_append(run, 23, 11.5f, 2.5f));
    CHECK(fx_glyph_run_count(run) == 2);

    glyphs = fx_glyph_run_data(run);
    CHECK(glyphs != NULL);
    CHECK(glyphs[0].glyph_id == 17);
    CHECK(glyphs[0].x == 1.5f);
    CHECK(glyphs[1].glyph_id == 23);
    CHECK(glyphs[1].x == 11.5f);

    fx_glyph_run_reset(run);
    CHECK(fx_glyph_run_count(run) == 0);

    fx_glyph_run_destroy(run);
    fx_font_destroy(font);
    FT_Done_FreeType(fake_ctx.ft_lib);
    return true;
}

static bool test_canvas_recording(void)
{
    fx_context fake_ctx = { 0 };
    FT_Init_FreeType(&fake_ctx.ft_lib);
    fx_surface surface = { 0 };
    uint32_t pixels[4] = {
        0x01020304u, 0x05060708u,
        0x090A0B0Cu, 0x0D0E0F10u,
    };
    fx_path *path = fx_path_create();
    fx_image *image = fx_image_create(&fake_ctx, &(fx_image_desc){
        .width = 2,
        .height = 2,
        .format = FX_FMT_RGBA8_UNORM,
        .data = pixels,
    });
    fx_font *font = fx_font_create(&fake_ctx, &(fx_font_desc){
        .family = "Mono",
        .size = 14.0f,
        .source_name = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    });
    fx_glyph_run *empty_run = fx_glyph_run_create(0);
    fx_glyph_run *run = fx_glyph_run_create(2);

    CHECK(path != NULL);
    CHECK(image != NULL);
    CHECK(empty_run != NULL);
    CHECK(run != NULL);
    CHECK(fx_path_add_rect(path, &(fx_rect){ 5.0f, 6.0f, 7.0f, 8.0f }));
    CHECK(fx_glyph_run_append(run, 9, 1.0f, 2.0f));

    surface.canvas.owner = &surface;
    fx_matrix_identity(&surface.canvas.current_matrix);
    CHECK(fx_canvas_op_count(&surface.canvas) == 0);

    fx_clear(&surface.canvas, fx_color_rgba(1, 2, 3, 255));
    CHECK(surface.canvas.has_clear);

    fx_paint paint;
    fx_paint_init(&paint, fx_color_rgba(10, 20, 30, 255));
    CHECK(fx_fill_path(&surface.canvas, path, &paint));

    paint.stroke_width = 0.0f;
    CHECK(!fx_stroke_path(&surface.canvas, path, &paint));
    
    paint.stroke_width = 2.0f;
    paint.color = fx_color_rgba(100, 110, 120, 255);
    CHECK(fx_stroke_path(&surface.canvas, path, &paint));

    CHECK(fx_draw_image(&surface.canvas, image, NULL,
                        &(fx_rect){ 20.0f, 30.0f, 40.0f, 50.0f }));

    paint.color = fx_color_rgba(255, 255, 255, 255);
    if (font) {
        CHECK(!fx_draw_glyph_run(&surface.canvas, font,
                                 empty_run, 0.0f, 0.0f, &paint));
        CHECK(fx_draw_glyph_run(&surface.canvas, font, run, 100.0f, 200.0f, &paint));
    }

    CHECK(fx_canvas_op_count(&surface.canvas) == (font ? 4 : 3));
    CHECK(surface.canvas.ops[0].kind == FX_OP_FILL_PATH);
    CHECK(surface.canvas.ops[0].u.fill_path.path == path);
    CHECK(surface.canvas.ops[1].kind == FX_OP_STROKE_PATH);
    CHECK(surface.canvas.ops[1].u.stroke_path.paint.stroke_width == 2.0f);
    CHECK(surface.canvas.ops[2].kind == FX_OP_DRAW_IMAGE);
    CHECK(surface.canvas.ops[2].u.draw_image.src.w == 2.0f);
    CHECK(surface.canvas.ops[2].u.draw_image.src.h == 2.0f);
    CHECK(surface.canvas.ops[2].u.draw_image.dst.x == 20.0f);
    if (font) {
        CHECK(surface.canvas.ops[3].kind == FX_OP_DRAW_GLYPHS);
        CHECK(surface.canvas.ops[3].u.draw_glyphs.run == run);
        CHECK(surface.canvas.ops[3].u.draw_glyphs.x == 100.0f);
    }

    surface.canvas.op_count = 0;
    surface.canvas.has_clear = false;
    surface.canvas.clear_color = 0;
    CHECK(fx_canvas_op_count(&surface.canvas) == 0);
    CHECK(!surface.canvas.has_clear);

    free(surface.canvas.ops);
    surface.canvas.ops = NULL;
    surface.canvas.op_cap = 0;
    CHECK(surface.canvas.ops == NULL);

    fx_glyph_run_destroy(empty_run);
    fx_glyph_run_destroy(run);
    if (font) fx_font_destroy(font);
    fx_image_destroy(image);
    fx_path_destroy(path);
    FT_Done_FreeType(fake_ctx.ft_lib);
    return true;
}

int main(void)
{
    if (!test_path_object()) return 1;
    if (!test_image_object()) return 1;
    if (!test_font_and_glyph_run()) return 1;
    if (!test_canvas_recording()) return 1;
    return 0;
}
