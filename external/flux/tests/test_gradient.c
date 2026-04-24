#include "flux/flux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
                    #cond, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

static bool pixel_near(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                       uint8_t er, uint8_t eg, uint8_t eb, uint8_t ea,
                       int tol)
{
    return abs((int)r - (int)er) <= tol &&
           abs((int)g - (int)eg) <= tol &&
           abs((int)b - (int)eb) <= tol &&
           abs((int)a - (int)ea) <= tol;
}

int main(void)
{
    fx_context_desc desc = {
        .app_name = "test_gradient",
        .enable_validation = false,
    };
    fx_context *ctx = fx_context_create(&desc);
    if (!ctx) {
        fprintf(stderr, "Vulkan not available, skipping GPU gradient test\n");
        return 0;
    }

    fx_surface *s = fx_surface_create_offscreen(ctx, 64, 64,
                                                FX_FMT_RGBA8_UNORM,
                                                FX_CS_SRGB);
    CHECK(s != NULL);

    /* --- Linear gradient: red → blue, left to right --- */
    fx_gradient *lin = fx_gradient_create_linear(ctx, &(fx_linear_gradient_desc){
        .start = { 0.0f, 0.0f },
        .end = { 64.0f, 0.0f },
        .colors = { fx_color_rgba(255, 0, 0, 255), fx_color_rgba(0, 0, 255, 255) },
        .stops = { 0.0f, 1.0f },
        .stop_count = 2,
    });
    CHECK(lin != NULL);

    fx_canvas *c = fx_surface_acquire(s);
    CHECK(c != NULL);

    fx_paint paint;
    fx_paint_init(&paint, fx_color_rgba(0, 0, 0, 255));
    fx_paint_set_gradient(&paint, lin);

    fx_path *rect_path = fx_path_create();
    fx_path_add_rect(rect_path, &(fx_rect){ 0.0f, 0.0f, 64.0f, 64.0f });
    fx_fill_path(c, rect_path, &paint);
    fx_surface_present(s);

    uint8_t *pixels = malloc(64 * 64 * 4);
    CHECK(fx_surface_read_pixels(s, pixels, 64 * 4));

    /* Left edge should be red-ish */
    CHECK(pixel_near(pixels[0], pixels[1], pixels[2], pixels[3],
                     255, 0, 0, 255, 20));
    /* Right edge should be blue-ish */
    CHECK(pixel_near(pixels[(63 * 64 + 63) * 4 + 0],
                     pixels[(63 * 64 + 63) * 4 + 1],
                     pixels[(63 * 64 + 63) * 4 + 2],
                     pixels[(63 * 64 + 63) * 4 + 3],
                     0, 0, 255, 255, 20));

    free(pixels);
    fx_gradient_destroy(lin);

    /* --- Radial gradient: green → yellow, centered --- */
    fx_gradient *rad = fx_gradient_create_radial(ctx, &(fx_radial_gradient_desc){
        .center = { 32.0f, 32.0f },
        .radius = 32.0f,
        .colors = { fx_color_rgba(0, 255, 0, 255), fx_color_rgba(255, 255, 0, 255) },
        .stops = { 0.0f, 1.0f },
        .stop_count = 2,
    });
    CHECK(rad != NULL);

    c = fx_surface_acquire(s);
    fx_paint_init(&paint, fx_color_rgba(0, 0, 0, 255));
    fx_paint_set_gradient(&paint, rad);
    rect_path = fx_path_create();
    fx_path_add_rect(rect_path, &(fx_rect){ 0.0f, 0.0f, 64.0f, 64.0f });
    fx_fill_path(c, rect_path, &paint);
    fx_surface_present(s);

    pixels = malloc(64 * 64 * 4);
    CHECK(fx_surface_read_pixels(s, pixels, 64 * 4));

    /* Center should be green-ish */
    CHECK(pixel_near(pixels[(32 * 64 + 32) * 4 + 0],
                     pixels[(32 * 64 + 32) * 4 + 1],
                     pixels[(32 * 64 + 32) * 4 + 2],
                     pixels[(32 * 64 + 32) * 4 + 3],
                     0, 255, 0, 255, 20));
    /* Corner should be yellow-ish */
    CHECK(pixel_near(pixels[(0 * 64 + 0) * 4 + 0],
                     pixels[(0 * 64 + 0) * 4 + 1],
                     pixels[(0 * 64 + 0) * 4 + 2],
                     pixels[(0 * 64 + 0) * 4 + 3],
                     255, 255, 0, 255, 20));

    free(pixels);
    fx_gradient_destroy(rad);

    fx_surface_destroy(s);
    fx_context_destroy(ctx);

    printf("gradient OK\n");
    return 0;
}
