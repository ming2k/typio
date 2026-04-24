#include "internal.h"

#include <stdio.h>

static const char *const k_required_device_exts[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
         VkDebugUtilsMessageTypeFlagsEXT         types,
         const VkDebugUtilsMessengerCallbackDataEXT *data,
         void                                   *user)
{
    (void)types;
    fx_context *ctx = user;
    fx_log_level lvl = FX_LOG_INFO;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        lvl = FX_LOG_ERROR;
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        lvl = FX_LOG_WARN;
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        lvl = FX_LOG_INFO;
    else
        lvl = FX_LOG_DEBUG;
    fx_log(ctx, lvl, "[vk] %s", data->pMessage);
    return VK_FALSE;
}

static bool has_layer(const char *name,
                      const VkLayerProperties *layers, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        if (strcmp(layers[i].layerName, name) == 0) return true;
    return false;
}

static bool has_ext(const char *name,
                    const VkExtensionProperties *exts, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        if (strcmp(exts[i].extensionName, name) == 0) return true;
    return false;
}

bool fx_instance_create(fx_context *ctx, const char *app_name,
                        bool want_validation,
                        const char *const *exts_wanted, uint32_t exts_wanted_n)
{
    /* Layers */
    uint32_t n_layers = 0;
    vkEnumerateInstanceLayerProperties(&n_layers, NULL);
    VkLayerProperties *layers = calloc(n_layers, sizeof(*layers));
    if (n_layers) vkEnumerateInstanceLayerProperties(&n_layers, layers);

    const char *validation_layer = "VK_LAYER_KHRONOS_validation";
    bool have_validation = want_validation
        && has_layer(validation_layer, layers, n_layers);
    free(layers);

    /* Extensions */
    uint32_t n_inst_exts = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &n_inst_exts, NULL);
    VkExtensionProperties *inst_exts = calloc(n_inst_exts, sizeof(*inst_exts));
    if (n_inst_exts)
        vkEnumerateInstanceExtensionProperties(NULL, &n_inst_exts, inst_exts);

    const char *enabled[16];
    uint32_t n_enabled = 0;
    for (uint32_t i = 0; i < exts_wanted_n && n_enabled < 16; ++i) {
        if (!has_ext(exts_wanted[i], inst_exts, n_inst_exts)) {
            FX_LOGE(ctx, "missing required instance extension: %s",
                    exts_wanted[i]);
            free(inst_exts);
            return false;
        }
        enabled[n_enabled++] = exts_wanted[i];
    }
    bool have_debug_utils =
        has_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, inst_exts, n_inst_exts);
    if (have_validation && have_debug_utils && n_enabled < 16)
        enabled[n_enabled++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    free(inst_exts);

    VkApplicationInfo app = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = app_name ? app_name : "flux",
        .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
        .pEngineName        = "flux",
        .engineVersion      = VK_MAKE_VERSION(0, 0, 1),
        .apiVersion         = VK_API_VERSION_1_2,
    };
    VkInstanceCreateInfo ci = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &app,
        .enabledExtensionCount   = n_enabled,
        .ppEnabledExtensionNames = enabled,
    };
    const char *layer_names[1] = { validation_layer };
    if (have_validation) {
        ci.enabledLayerCount   = 1;
        ci.ppEnabledLayerNames = layer_names;
    }

    VkResult r = vkCreateInstance(&ci, NULL, &ctx->instance);
    if (r != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateInstance failed: %d", (int)r);
        return false;
    }
    ctx->validation_enabled = have_validation;

    if (have_validation && have_debug_utils) {
        PFN_vkCreateDebugUtilsMessengerEXT create_fn =
            (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(ctx->instance,
                                  "vkCreateDebugUtilsMessengerEXT");
        if (create_fn) {
            VkDebugUtilsMessengerCreateInfoEXT mi = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                .messageSeverity =
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
                .messageType =
                    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                .pfnUserCallback = debug_cb,
                .pUserData       = ctx,
            };
            create_fn(ctx->instance, &mi, NULL, &ctx->debug_messenger);
        }
    }
    return true;
}

void fx_instance_destroy(fx_context *ctx)
{
    if (ctx->debug_messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_fn =
            (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(ctx->instance,
                                  "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_fn)
            destroy_fn(ctx->instance, ctx->debug_messenger, NULL);
        ctx->debug_messenger = VK_NULL_HANDLE;
    }
    if (ctx->instance) {
        vkDestroyInstance(ctx->instance, NULL);
        ctx->instance = VK_NULL_HANDLE;
    }
}

/* Picks a queue family with graphics + (optional) present-to-surface.
 * Returns UINT32_MAX if none matches. */
static uint32_t pick_graphics_family(VkPhysicalDevice p, VkSurfaceKHR surface)
{
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(p, &n, NULL);
    VkQueueFamilyProperties *qf = calloc(n, sizeof(*qf));
    vkGetPhysicalDeviceQueueFamilyProperties(p, &n, qf);
    uint32_t pick = UINT32_MAX;
    for (uint32_t i = 0; i < n; ++i) {
        if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        if (surface != VK_NULL_HANDLE) {
            VkBool32 supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(p, i, surface, &supported);
            if (!supported) continue;
        }
        pick = i;
        break;
    }
    free(qf);
    return pick;
}

static int score_device(VkPhysicalDevice p)
{
    VkPhysicalDeviceProperties pr;
    vkGetPhysicalDeviceProperties(p, &pr);
    int s = 0;
    if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   s += 1000;
    else if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) s += 500;
    else if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)    s += 250;
    return s;
}

bool fx_device_init(fx_context *ctx, VkSurfaceKHR probe_surface)
{
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &n, NULL);
    if (!n) {
        FX_LOGE(ctx, "no Vulkan physical devices found");
        return false;
    }
    VkPhysicalDevice *devs = calloc(n, sizeof(*devs));
    vkEnumeratePhysicalDevices(ctx->instance, &n, devs);

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int best_score = -1;
    uint32_t best_family = UINT32_MAX;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t fam = pick_graphics_family(devs[i], probe_surface);
        if (fam == UINT32_MAX) continue;

        /* Check swapchain extension on this device. */
        uint32_t ne = 0;
        vkEnumerateDeviceExtensionProperties(devs[i], NULL, &ne, NULL);
        VkExtensionProperties *de = calloc(ne, sizeof(*de));
        vkEnumerateDeviceExtensionProperties(devs[i], NULL, &ne, de);
        bool ok = true;
        for (size_t k = 0; k < sizeof(k_required_device_exts)/sizeof(*k_required_device_exts); ++k) {
            if (!has_ext(k_required_device_exts[k], de, ne)) { ok = false; break; }
        }
        free(de);
        if (!ok) continue;

        int s = score_device(devs[i]);
        if (s > best_score) {
            best_score = s;
            best = devs[i];
            best_family = fam;
        }
    }
    free(devs);

    if (best == VK_NULL_HANDLE) {
        FX_LOGE(ctx, "no suitable physical device");
        return false;
    }
    ctx->phys = best;
    ctx->graphics_family = best_family;
    ctx->queue_supports_present = probe_surface != VK_NULL_HANDLE;
    vkGetPhysicalDeviceProperties(ctx->phys, &ctx->phys_props);
    vkGetPhysicalDeviceMemoryProperties(ctx->phys, &ctx->mem_props);
    FX_LOGI(ctx, "picked GPU: %s (api %u.%u.%u)",
            ctx->phys_props.deviceName,
            VK_VERSION_MAJOR(ctx->phys_props.apiVersion),
            VK_VERSION_MINOR(ctx->phys_props.apiVersion),
            VK_VERSION_PATCH(ctx->phys_props.apiVersion));

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx->graphics_family,
        .queueCount       = 1,
        .pQueuePriorities = &prio,
    };
    VkPhysicalDeviceFeatures feats = {0};
    VkDeviceCreateInfo dci = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &qci,
        .enabledExtensionCount   = (uint32_t)(sizeof(k_required_device_exts)/sizeof(*k_required_device_exts)),
        .ppEnabledExtensionNames = k_required_device_exts,
        .pEnabledFeatures        = &feats,
    };
    VkResult r = vkCreateDevice(ctx->phys, &dci, NULL, &ctx->device);
    if (r != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateDevice failed: %d", (int)r);
        return false;
    }
    vkGetDeviceQueue(ctx->device, ctx->graphics_family, 0,
                     &ctx->graphics_queue);

    VkCommandPoolCreateInfo pci = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->graphics_family,
    };
    FX_CHECK_VK(ctx, vkCreateCommandPool(ctx->device, &pci, NULL,
                                         &ctx->frame_cmd_pool));
    return true;
}

void fx_device_shutdown(fx_context *ctx)
{
    if (ctx->device) {
        vkDeviceWaitIdle(ctx->device);
        if (ctx->frame_cmd_pool) {
            vkDestroyCommandPool(ctx->device, ctx->frame_cmd_pool, NULL);
            ctx->frame_cmd_pool = VK_NULL_HANDLE;
        }
        vkDestroyDevice(ctx->device, NULL);
        ctx->device = VK_NULL_HANDLE;
    }
    ctx->queue_supports_present = false;
    fx_instance_destroy(ctx);
}
