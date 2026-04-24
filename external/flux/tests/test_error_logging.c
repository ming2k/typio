#include "internal.h"
#include <stdio.h>
#include <string.h>

static int g_error_count = 0;
static int g_warn_count = 0;

static void test_log_fn(fx_log_level level, const char *msg, void *user)
{
    (void)user;
    if (level == FX_LOG_ERROR) {
        g_error_count++;
        fprintf(stderr, "[ERROR] %s\n", msg);
    } else if (level == FX_LOG_WARN) {
        g_warn_count++;
    }
}

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
                    #cond, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

static bool test_no_errors_on_valid_usage(void)
{
    fx_context_desc desc = {
        .app_name = "test_error_logging",
        .log = test_log_fn,
        .log_user = NULL,
        .enable_validation = false,
    };

    g_error_count = 0;
    g_warn_count = 0;

    fx_context *ctx = fx_context_create(&desc);
    if (!ctx) {
        /* Vulkan not available is OK for this test, but we still check
         * that no errors were emitted during the attempt. */
        CHECK(g_error_count == 0);
        return true;
    }

    /* Basic surface lifecycle */
    fx_surface *s = fx_surface_create_offscreen(ctx, 32, 32,
                                                FX_FMT_RGBA8_UNORM,
                                                FX_CS_SRGB);
    if (s) {
        fx_canvas *c = fx_surface_acquire(s);
        if (c) {
            fx_clear(c, fx_color_rgba(0, 0, 0, 255));
        }
        fx_surface_present(s);
        fx_surface_destroy(s);
    }

    fx_context_destroy(ctx);

    CHECK(g_error_count == 0);
    return true;
}

static bool test_errors_on_null_image_create(void)
{
    g_error_count = 0;
    fx_image *img = fx_image_create(NULL, NULL);
    CHECK(img == NULL);
    /* fx_image_create with NULL args should fail gracefully */
    return true;
}

static bool test_image_create_null_desc(void)
{
    fx_context fake_ctx = { 0 };
    g_error_count = 0;
    fx_image *img = fx_image_create(&fake_ctx, NULL);
    CHECK(img == NULL);
    return true;
}

int main(void)
{
    if (!test_no_errors_on_valid_usage()) return 1;
    if (!test_errors_on_null_image_create()) return 1;
    if (!test_image_create_null_desc()) return 1;
    printf("error_logging OK\n");
    return 0;
}
