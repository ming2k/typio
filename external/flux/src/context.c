#include "flux/flux_vulkan.h"
#include "internal.h"

static void default_log(fx_log_level lvl, const char *msg, void *user)
{
    (void)user;
    const char *tag = "?";
    switch (lvl) {
        case FX_LOG_ERROR: tag = "E"; break;
        case FX_LOG_WARN:  tag = "W"; break;
        case FX_LOG_INFO:  tag = "I"; break;
        case FX_LOG_DEBUG: tag = "D"; break;
    }
    fprintf(stderr, "[flux %s] %s\n", tag, msg);
}

void fx_log(const fx_context *ctx, fx_log_level lvl, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (ctx && ctx->log) ctx->log(lvl, buf, ctx->log_user);
    else                 default_log(lvl, buf, NULL);
}

static bool env_flag(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] && v[0] != '0';
}

fx_context *fx_context_create(const fx_context_desc *desc)
{
    fx_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    if (desc) {
        ctx->log      = desc->log;
        ctx->log_user = desc->log_user;
    }

    bool want_validation = desc && desc->enable_validation;
    if (!want_validation) want_validation = env_flag("FX_ENABLE_VALIDATION");

    /* Always include Wayland surface support; offscreen-only
     * contexts simply do not create a surface. */
    const char *inst_exts[] = {
        "VK_KHR_surface",
        "VK_KHR_wayland_surface",
    };
    if (!fx_instance_create(ctx,
                            desc ? desc->app_name : NULL,
                            want_validation,
                            inst_exts,
                            (uint32_t)(sizeof(inst_exts)/sizeof(*inst_exts)))) {
        free(ctx);
        return NULL;
    }

    VkPipelineCacheCreateInfo pcci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    };
    /* Device may not exist yet; pipeline_cache is created on first surface. */
    (void)pcci;

    /* Defer device creation until the first surface: surface support is
     * a queue-family selection input. */
    if (FT_Init_FreeType(&ctx->ft_lib) != 0) {
        FX_LOGE(ctx, "failed to initialize FreeType");
        fx_device_shutdown(ctx);
        free(ctx);
        return NULL;
    }
    return ctx;
}

VkInstance fx_context_get_instance(fx_context *ctx)
{
    return ctx ? ctx->instance : VK_NULL_HANDLE;
}

bool fx_context_get_device_caps(const fx_context *ctx, fx_device_caps *out_caps)
{
    VkFormatProperties fmt_props;

    if (!ctx || !ctx->phys || !out_caps) return false;

    memset(out_caps, 0, sizeof(*out_caps));
    out_caps->validation_enabled = ctx->validation_enabled;
    out_caps->graphics_queue = ctx->device != VK_NULL_HANDLE;
    out_caps->present_queue = ctx->queue_supports_present;
    out_caps->api_version = ctx->phys_props.apiVersion;
    out_caps->max_image_dimension_2d =
        ctx->phys_props.limits.maxImageDimension2D;
    out_caps->max_color_attachments =
        ctx->phys_props.limits.maxColorAttachments;

    vkGetPhysicalDeviceFormatProperties(ctx->phys, VK_FORMAT_R8G8B8A8_UNORM,
                                        &fmt_props);
    out_caps->sampled_images =
        (fmt_props.optimalTilingFeatures &
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    out_caps->storage_images =
        (fmt_props.optimalTilingFeatures &
         VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
    return true;
}

void fx_context_destroy(fx_context *ctx)
{
    if (!ctx) return;
    if (ctx->ft_lib) FT_Done_FreeType(ctx->ft_lib);
    fx_device_shutdown(ctx);
    free(ctx);
}
