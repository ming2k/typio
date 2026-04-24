#include "flux/flux.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static uint8_t r(fx_color c) { return (c >> 16) & 0xFF; }
static uint8_t g(fx_color c) { return (c >>  8) & 0xFF; }
static uint8_t b(fx_color c) { return (c      ) & 0xFF; }
static uint8_t a(fx_color c) { return (c >> 24) & 0xFF; }

/* Premultiplied colors for expected values */
static fx_color RED   = 0xFFFF0000;
static fx_color GREEN = 0xFF00FF00;
static fx_color BLUE  = 0xFF0000FF;

static int approx_eq(uint8_t a, uint8_t b)
{
    int d = (int)a - (int)b;
    return d < 0 ? -d : d;
}

static void check_pixel(const uint8_t *px, fx_color expected, int x, int y)
{
    uint8_t er = r(expected), eg = g(expected), eb = b(expected), ea = a(expected);
    if (approx_eq(px[0], er) > 8 ||
        approx_eq(px[1], eg) > 8 ||
        approx_eq(px[2], eb) > 8 ||
        approx_eq(px[3], ea) > 8) {
        fprintf(stderr,
                "pixel mismatch at (%d,%d): got (%u,%u,%u,%u) "
                "expected (%u,%u,%u,%u)\n",
                x, y, px[0], px[1], px[2], px[3], er, eg, eb, ea);
        exit(1);
    }
}

static void test_clip_rect(void)
{
    fx_context_desc desc = { .app_name = "test_clip" };
    fx_context *ctx = fx_context_create(&desc);
    assert(ctx);

    fx_surface *s = fx_surface_create_offscreen(ctx, 64, 64,
                                                FX_FMT_RGBA8_UNORM,
                                                FX_CS_SRGB);
    assert(s);

    fx_canvas *c = fx_surface_acquire(s);
    fx_clear(c, RED);

    fx_rect clip = { .x = 0, .y = 0, .w = 16, .h = 16 };
    fx_clip_rect(c, &clip);

    fx_rect fill = { .x = 0, .y = 0, .w = 64, .h = 64 };
    fx_fill_rect(c, &fill, BLUE);

    fx_surface_present(s);

    uint8_t *pixels = malloc(64 * 64 * 4);
    assert(pixels);
    assert(fx_surface_read_pixels(s, pixels, 64 * 4));

    /* Inside clip: blue */
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            check_pixel(&pixels[(y * 64 + x) * 4], BLUE, x, y);
        }
    }

    /* Outside clip: still red (clear color) */
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            if (x < 16 && y < 16) continue;
            check_pixel(&pixels[(y * 64 + x) * 4], RED, x, y);
        }
    }

    free(pixels);
    fx_surface_destroy(s);
    fx_context_destroy(ctx);
    printf("test_clip_rect: OK\n");
}

static void test_clip_reset(void)
{
    fx_context_desc desc = { .app_name = "test_clip_reset" };
    fx_context *ctx = fx_context_create(&desc);
    assert(ctx);

    fx_surface *s = fx_surface_create_offscreen(ctx, 64, 64,
                                                FX_FMT_RGBA8_UNORM,
                                                FX_CS_SRGB);
    assert(s);

    fx_canvas *c = fx_surface_acquire(s);
    fx_clear(c, RED);

    /* Clip to top-left 16x16 and fill blue */
    fx_rect clip = { .x = 0, .y = 0, .w = 16, .h = 16 };
    fx_clip_rect(c, &clip);
    fx_rect fill = { .x = 0, .y = 0, .w = 64, .h = 64 };
    fx_fill_rect(c, &fill, BLUE);

    /* Reset clip and fill entire surface green */
    fx_reset_clip(c);
    fx_fill_rect(c, &fill, GREEN);

    fx_surface_present(s);

    uint8_t *pixels = malloc(64 * 64 * 4);
    assert(pixels);
    assert(fx_surface_read_pixels(s, pixels, 64 * 4));

    /* After reset + full green fill, everything should be green */
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            check_pixel(&pixels[(y * 64 + x) * 4], GREEN, x, y);
        }
    }

    free(pixels);
    fx_surface_destroy(s);
    fx_context_destroy(ctx);
    printf("test_clip_reset: OK\n");
}

static void test_clip_partial(void)
{
    fx_context_desc desc = { .app_name = "test_clip_partial" };
    fx_context *ctx = fx_context_create(&desc);
    assert(ctx);

    fx_surface *s = fx_surface_create_offscreen(ctx, 64, 64,
                                                FX_FMT_RGBA8_UNORM,
                                                FX_CS_SRGB);
    assert(s);

    fx_canvas *c = fx_surface_acquire(s);
    fx_clear(c, RED);

    /* Clip to center 32x32 and fill blue */
    fx_rect clip = { .x = 16, .y = 16, .w = 32, .h = 32 };
    fx_clip_rect(c, &clip);
    fx_rect fill = { .x = 0, .y = 0, .w = 64, .h = 64 };
    fx_fill_rect(c, &fill, BLUE);

    fx_surface_present(s);

    uint8_t *pixels = malloc(64 * 64 * 4);
    assert(pixels);
    assert(fx_surface_read_pixels(s, pixels, 64 * 4));

    /* Center 32x32 should be blue */
    for (int y = 16; y < 48; ++y) {
        for (int x = 16; x < 48; ++x) {
            check_pixel(&pixels[(y * 64 + x) * 4], BLUE, x, y);
        }
    }

    /* Everything else should be red */
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            if (x >= 16 && x < 48 && y >= 16 && y < 48) continue;
            check_pixel(&pixels[(y * 64 + x) * 4], RED, x, y);
        }
    }

    free(pixels);
    fx_surface_destroy(s);
    fx_context_destroy(ctx);
    printf("test_clip_partial: OK\n");
}

static void test_clip_path(void)
{
    fx_context_desc desc = { .app_name = "test_clip_path" };
    fx_context *ctx = fx_context_create(&desc);
    assert(ctx);

    fx_surface *s = fx_surface_create_offscreen(ctx, 64, 64,
                                                FX_FMT_RGBA8_UNORM,
                                                FX_CS_SRGB);
    assert(s);

    /* Create a triangle path in the top-left quadrant.
     * The bounding box is (0,0)-(32,32). */
    fx_path *path = fx_path_create();
    assert(path);
    fx_path_move_to(path, 0, 0);
    fx_path_line_to(path, 32, 0);
    fx_path_line_to(path, 0, 32);
    fx_path_close(path);

    fx_canvas *c = fx_surface_acquire(s);
    fx_clear(c, RED);
    fx_clip_path(c, path);

    fx_rect fill = { .x = 0, .y = 0, .w = 64, .h = 64 };
    fx_fill_rect(c, &fill, BLUE);

    fx_surface_present(s);

    uint8_t *pixels = malloc(64 * 64 * 4);
    assert(pixels);
    assert(fx_surface_read_pixels(s, pixels, 64 * 4));

    /* Inside the bounding box (e.g. 10,10): blue */
    check_pixel(&pixels[(10 * 64 + 10) * 4], BLUE, 10, 10);

    /* Outside the bounding box (e.g. 40,40): red */
    check_pixel(&pixels[(40 * 64 + 40) * 4], RED, 40, 40);

    free(pixels);
    fx_path_destroy(path);
    fx_surface_destroy(s);
    fx_context_destroy(ctx);
    printf("test_clip_path: OK\n");
}

int main(void)
{
    test_clip_rect();
    test_clip_reset();
    test_clip_partial();
    test_clip_path();
    printf("All clip tests passed.\n");
    return 0;
}
