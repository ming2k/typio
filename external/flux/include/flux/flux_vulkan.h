/*
 * flux - Vulkan interop entry points.
 *
 * Include this header only where callers need to pass externally-created
 * Vulkan presentation objects to flux or inspect the Vulkan instance used by
 * a context. Core drawing code should include <flux/flux.h> instead.
 */
#ifndef FLUX_VULKAN_H
#define FLUX_VULKAN_H

#include "flux.h"

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

FX_API VkInstance fx_context_get_instance(fx_context *ctx);

FX_API fx_surface *fx_surface_create_vulkan(fx_context *ctx,
                                            VkSurfaceKHR vk_surface,
                                            int32_t width,
                                            int32_t height,
                                            fx_color_space cs);

#ifdef __cplusplus
}
#endif

#endif /* FLUX_VULKAN_H */
