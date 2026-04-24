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

static bool test_image_update_pixels(void)
{
    fx_context fake_ctx = { 0 };
    uint32_t initial[4] = {
        0xFF0000FFu, 0x00FF00FFu,
        0x0000FFFFu, 0xFFFFFFFFu,
    };
    uint32_t updated[4] = {
        0xAABBCCDDu, 0x11223344u,
        0x55667788u, 0x99AABBCCu,
    };

    fx_image *image = fx_image_create(&fake_ctx, &(fx_image_desc){
        .width = 2,
        .height = 2,
        .format = FX_FMT_RGBA8_UNORM,
        .data = initial,
    });
    CHECK(image != NULL);

    /* Verify initial copy */
    size_t size = 0, stride = 0;
    const uint32_t *data = fx_image_data(image, &size, &stride);
    CHECK(data != NULL);
    CHECK(memcmp(data, initial, sizeof(initial)) == 0);

    /* Update and verify */
    CHECK(fx_image_update(image, updated, 8));
    data = fx_image_data(image, &size, &stride);
    CHECK(data != NULL);
    CHECK(memcmp(data, updated, sizeof(updated)) == 0);

    /* Ensure old data is truly replaced */
    CHECK(data[0] == 0xAABBCCDDu);
    CHECK(data[3] == 0x99AABBCCu);

    fx_image_destroy(image);
    return true;
}

static bool test_image_update_null_safety(void)
{
    CHECK(!fx_image_update(NULL, NULL, 0));
    return true;
}

int main(void)
{
    if (!test_image_update_pixels()) return 1;
    if (!test_image_update_null_safety()) return 1;
    printf("image_update OK\n");
    return 0;
}
