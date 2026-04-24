#include "flux/flux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    fx_context_desc desc = {
        .app_name = "test_offscreen",
        .enable_validation = false,
    };
    fx_context *ctx = fx_context_create(&desc);
    if (!ctx) {
        fprintf(stderr, "failed to create context\n");
        return 1;
    }

    fx_surface *s = fx_surface_create_offscreen(ctx, 64, 64,
                                                FX_FMT_RGBA8_UNORM,
                                                FX_CS_SRGB);
    if (!s) {
        fprintf(stderr, "failed to create offscreen surface\n");
        fx_context_destroy(ctx);
        return 1;
    }

    fx_canvas *c = fx_surface_acquire(s);
    if (!c) {
        fprintf(stderr, "failed to acquire canvas\n");
        fx_surface_destroy(s);
        fx_context_destroy(ctx);
        return 1;
    }

    fx_clear(c, fx_color_rgba(255, 0, 0, 255));
    fx_surface_present(s);

    uint8_t *pixels = malloc(64 * 64 * 4);
    if (!fx_surface_read_pixels(s, pixels, 64 * 4)) {
        fprintf(stderr, "failed to read pixels\n");
        free(pixels);
        fx_surface_destroy(s);
        fx_context_destroy(ctx);
        return 1;
    }

    int errors = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            uint8_t r = pixels[(y * 64 + x) * 4 + 0];
            uint8_t g = pixels[(y * 64 + x) * 4 + 1];
            uint8_t b = pixels[(y * 64 + x) * 4 + 2];
            uint8_t a = pixels[(y * 64 + x) * 4 + 3];
            if (r != 255 || g != 0 || b != 0 || a != 255) {
                if (errors < 5) {
                    fprintf(stderr, "pixel(%d,%d) = %u,%u,%u,%u\n",
                            x, y, r, g, b, a);
                }
                errors++;
            }
        }
    }

    free(pixels);
    fx_surface_destroy(s);
    fx_context_destroy(ctx);

    if (errors) {
        fprintf(stderr, "%d pixels wrong\n", errors);
        return 1;
    }

    printf("offscreen OK\n");
    return 0;
}
