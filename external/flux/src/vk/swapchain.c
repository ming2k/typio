#include "internal.h"
#include "vk/vk_mem_alloc.h"

#include "solid_color_vert_spv.inc"
#include "solid_color_frag_spv.inc"
#include "image_vert_spv.inc"
#include "image_frag_spv.inc"
#include "text_frag_spv.inc"
#include "gradient_vert_spv.inc"
#include "gradient_frag_spv.inc"
#include "stencil_frag_spv.inc"
#include "blur_frag_spv.inc"

static VkSurfaceFormatKHR pick_format(const VkSurfaceFormatKHR *formats,
                                      uint32_t n)
{
    /* Prefer BGRA8 SRGB, then RGBA8 SRGB, then any UNORM BGRA. */
    for (uint32_t i = 0; i < n; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return formats[i];
    for (uint32_t i = 0; i < n; ++i)
        if (formats[i].format == VK_FORMAT_R8G8B8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return formats[i];
    for (uint32_t i = 0; i < n; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
            return formats[i];
    return formats[0];
}

static VkPresentModeKHR pick_present_mode(const VkPresentModeKHR *modes,
                                          uint32_t n)
{
    /* Priority per §4.4. */
    const VkPresentModeKHR prefs[] = {
        VK_PRESENT_MODE_FIFO_RELAXED_KHR,
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
    };
    for (size_t p = 0; p < sizeof(prefs)/sizeof(*prefs); ++p)
        for (uint32_t i = 0; i < n; ++i)
            if (modes[i] == prefs[p]) return prefs[p];
    return VK_PRESENT_MODE_FIFO_KHR;  /* guaranteed */
}

static VkExtent2D clamp_extent(VkSurfaceCapabilitiesKHR caps,
                               int32_t w, int32_t h)
{
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;
    VkExtent2D e = { (uint32_t)w, (uint32_t)h };
    if (e.width  < caps.minImageExtent.width)  e.width  = caps.minImageExtent.width;
    if (e.height < caps.minImageExtent.height) e.height = caps.minImageExtent.height;
    if (e.width  > caps.maxImageExtent.width)  e.width  = caps.maxImageExtent.width;
    if (e.height > caps.maxImageExtent.height) e.height = caps.maxImageExtent.height;
    return e;
}

bool fx_make_render_pass(fx_surface *s, VkImageLayout final_layout)
{
    VkAttachmentDescription attachments[2] = { 0 };
    attachments[0] = (VkAttachmentDescription){
        .format         = s->surface_format.format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = final_layout,
    };
    attachments[1] = (VkAttachmentDescription){
        .format         = VK_FORMAT_S8_UINT,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference stencil_ref = {
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount    = 1,
        .pColorAttachments       = &color_ref,
        .pDepthStencilAttachment = &stencil_ref,
    };
    VkSubpassDependency deps[2] = {
        {
            .srcSubpass    = VK_SUBPASS_EXTERNAL,
            .dstSubpass    = 0,
            .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        },
    };
    VkRenderPassCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments    = attachments,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = deps,
    };
    VkResult r = vkCreateRenderPass(s->ctx->device, &ci, NULL, &s->render_pass);
    if (r != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkCreateRenderPass: %d", (int)r);
        return false;
    }
    return true;
}

static bool fx_make_template_render_pass(fx_context *ctx, fx_pipeline_set *ps)
{
    VkAttachmentDescription attachments[2] = { 0 };
    attachments[0] = (VkAttachmentDescription){
        .format         = ps->format,
        .samples        = ps->samples,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    attachments[1] = (VkAttachmentDescription){
        .format         = VK_FORMAT_S8_UINT,
        .samples        = ps->samples,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference stencil_ref = {
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount    = 1,
        .pColorAttachments       = &color_ref,
        .pDepthStencilAttachment = &stencil_ref,
    };
    VkSubpassDependency deps[2] = {
        {
            .srcSubpass    = VK_SUBPASS_EXTERNAL,
            .dstSubpass    = 0,
            .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        },
    };
    VkRenderPassCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments    = attachments,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = deps,
    };
    VkResult r = vkCreateRenderPass(ctx->device, &ci, NULL, &ps->template_render_pass);
    if (r != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateRenderPass (template): %d", (int)r);
        return false;
    }
    return true;
}

static void fx_pipeline_set_destroy_one(fx_context *ctx, fx_pipeline_set *ps)
{
    VkDevice dev = ctx->device;
    if (ps->blur_pipeline)           { vkDestroyPipeline(dev, ps->blur_pipeline, NULL); ps->blur_pipeline = VK_NULL_HANDLE; }
    if (ps->blur_layout)             { vkDestroyPipelineLayout(dev, ps->blur_layout, NULL); ps->blur_layout = VK_NULL_HANDLE; }
    if (ps->gradient_cover_pipeline) { vkDestroyPipeline(dev, ps->gradient_cover_pipeline, NULL); ps->gradient_cover_pipeline = VK_NULL_HANDLE; }
    if (ps->solid_cover_pipeline)    { vkDestroyPipeline(dev, ps->solid_cover_pipeline, NULL); ps->solid_cover_pipeline = VK_NULL_HANDLE; }
    if (ps->fill_stencil_pipeline)   { vkDestroyPipeline(dev, ps->fill_stencil_pipeline, NULL); ps->fill_stencil_pipeline = VK_NULL_HANDLE; }
    if (ps->stencil_pipeline)        { vkDestroyPipeline(dev, ps->stencil_pipeline, NULL); ps->stencil_pipeline = VK_NULL_HANDLE; }
    if (ps->stencil_layout)          { vkDestroyPipelineLayout(dev, ps->stencil_layout, NULL); ps->stencil_layout = VK_NULL_HANDLE; }
    if (ps->gradient_pipeline)   { vkDestroyPipeline(dev, ps->gradient_pipeline, NULL); ps->gradient_pipeline = VK_NULL_HANDLE; }
    if (ps->gradient_layout)     { vkDestroyPipelineLayout(dev, ps->gradient_layout, NULL); ps->gradient_layout = VK_NULL_HANDLE; }
    if (ps->text_pipeline)       { vkDestroyPipeline(dev, ps->text_pipeline, NULL); ps->text_pipeline = VK_NULL_HANDLE; }
    if (ps->text_layout)         { vkDestroyPipelineLayout(dev, ps->text_layout, NULL); ps->text_layout = VK_NULL_HANDLE; }
    if (ps->image_pipeline)      { vkDestroyPipeline(dev, ps->image_pipeline, NULL); ps->image_pipeline = VK_NULL_HANDLE; }
    if (ps->image_layout)        { vkDestroyPipelineLayout(dev, ps->image_layout, NULL); ps->image_layout = VK_NULL_HANDLE; }
    if (ps->solid_rect_pipeline) { vkDestroyPipeline(dev, ps->solid_rect_pipeline, NULL); ps->solid_rect_pipeline = VK_NULL_HANDLE; }
    if (ps->solid_rect_layout)   { vkDestroyPipelineLayout(dev, ps->solid_rect_layout, NULL); ps->solid_rect_layout = VK_NULL_HANDLE; }
    if (ps->image_dsl)           { vkDestroyDescriptorSetLayout(dev, ps->image_dsl, NULL); ps->image_dsl = VK_NULL_HANDLE; }
    if (ps->template_render_pass){ vkDestroyRenderPass(dev, ps->template_render_pass, NULL); ps->template_render_pass = VK_NULL_HANDLE; }
}

static bool fx_make_fill_stencil_pipeline(fx_pipeline_set *ps, fx_context *ctx);
static bool fx_make_solid_cover_pipeline(fx_pipeline_set *ps, fx_context *ctx);
static bool fx_make_gradient_cover_pipeline(fx_pipeline_set *ps, fx_context *ctx);

fx_pipeline_set *fx_pipeline_set_get(fx_context *ctx,
                                     VkFormat color_format,
                                     VkSampleCountFlagBits samples)
{
    for (uint32_t i = 0; i < ctx->pipeline_set_count; ++i) {
        fx_pipeline_set *ps = &ctx->pipeline_sets[i];
        if (ps->format == color_format && ps->samples == samples)
            return ps;
    }
    if (ctx->pipeline_set_count >= FX_MAX_PIPELINE_SETS) {
        FX_LOGE(ctx, "pipeline set limit reached");
        return NULL;
    }
    fx_pipeline_set *ps = &ctx->pipeline_sets[ctx->pipeline_set_count++];
    memset(ps, 0, sizeof(*ps));
    ps->format = color_format;
    ps->samples = samples;

    if (!fx_make_template_render_pass(ctx, ps)) goto fail;
    if (!fx_make_image_dsl(ps, ctx)) goto fail;
    if (!fx_make_solid_pipeline(ps, ctx)) goto fail;
    if (!fx_make_image_pipeline(ps, ctx)) goto fail;
    if (!fx_make_text_pipeline(ps, ctx)) goto fail;
    if (!fx_make_gradient_pipeline(ps, ctx)) goto fail;
    if (!fx_make_stencil_pipeline(ps, ctx)) goto fail;
    if (!fx_make_fill_stencil_pipeline(ps, ctx)) goto fail;
    if (!fx_make_solid_cover_pipeline(ps, ctx)) goto fail;
    if (!fx_make_gradient_cover_pipeline(ps, ctx)) goto fail;
    if (!fx_make_blur_pipeline(ps, ctx)) goto fail;

    return ps;
fail:
    fx_pipeline_set_destroy_one(ctx, ps);
    ctx->pipeline_set_count--;
    return NULL;
}

void fx_pipeline_set_destroy_all(fx_context *ctx)
{
    for (uint32_t i = 0; i < ctx->pipeline_set_count; ++i)
        fx_pipeline_set_destroy_one(ctx, &ctx->pipeline_sets[i]);
    ctx->pipeline_set_count = 0;
}

static VkShaderModule make_shader_module(fx_context *ctx,
                                         const uint32_t *code,
                                         size_t code_size)
{
    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size,
        .pCode    = code,
    };
    if (vkCreateShaderModule(ctx->device, &ci, NULL, &module) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateShaderModule failed");
        return VK_NULL_HANDLE;
    }
    return module;
}

static bool make_pipeline_core(
    fx_context *ctx,
    VkPipelineLayout layout,
    VkRenderPass render_pass,
    const uint32_t *vert_spv, size_t vert_size,
    const uint32_t *frag_spv, size_t frag_size,
    size_t vertex_stride,
    const VkVertexInputAttributeDescription *attrs, uint32_t attr_count,
    VkBool32 blend_enable,
    VkBlendFactor src_color, VkBlendFactor dst_color,
    VkBlendFactor src_alpha, VkBlendFactor dst_alpha,
    VkColorComponentFlags color_write_mask,
    VkStencilOp stencil_pass_op,
    VkCompareOp stencil_compare_op,
    uint32_t stencil_write_mask,
    uint32_t stencil_reference,
    VkPipeline *out_pipeline,
    const char *name)
{
    VkShaderModule vert = make_shader_module(ctx, vert_spv, vert_size);
    VkShaderModule frag = make_shader_module(ctx, frag_spv, frag_size);
    if (!vert || !frag) goto fail;

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,   .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
    };

    VkVertexInputBindingDescription binding = {
        .binding = 0, .stride = (uint32_t)vertex_stride, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = attr_count, .pVertexAttributeDescriptions = attrs,
    };

    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState blend_att = {
        .blendEnable = blend_enable,
        .srcColorBlendFactor = src_color, .dstColorBlendFactor = dst_color, .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = src_alpha, .dstAlphaBlendFactor = dst_alpha, .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = color_write_mask,
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_att,
    };
    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE, .depthWriteEnable = VK_FALSE, .stencilTestEnable = VK_TRUE,
        .front = { .failOp = VK_STENCIL_OP_KEEP, .passOp = stencil_pass_op, .depthFailOp = VK_STENCIL_OP_KEEP,
                   .compareOp = stencil_compare_op, .compareMask = 0xFF, .writeMask = stencil_write_mask, .reference = stencil_reference },
        .back = { .failOp = VK_STENCIL_OP_KEEP, .passOp = stencil_pass_op, .depthFailOp = VK_STENCIL_OP_KEEP,
                  .compareOp = stencil_compare_op, .compareMask = 0xFF, .writeMask = stencil_write_mask, .reference = stencil_reference },
    };
    VkDynamicState dyn_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 3, .pDynamicStates = dyn_states,
    };

    VkGraphicsPipelineCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vi, .pInputAssemblyState = &ia,
        .pViewportState = &vp, .pRasterizationState = &rs,
        .pMultisampleState = &ms, .pDepthStencilState = &ds,
        .pColorBlendState = &cb, .pDynamicState = &dyn,
        .layout = layout, .renderPass = render_pass, .subpass = 0,
    };

    if (vkCreateGraphicsPipelines(ctx->device, ctx->pipeline_cache, 1, &pci, NULL, out_pipeline) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateGraphicsPipelines (%s) failed", name);
        goto fail;
    }

    vkDestroyShaderModule(ctx->device, frag, NULL);
    vkDestroyShaderModule(ctx->device, vert, NULL);
    return true;

fail:
    if (frag) vkDestroyShaderModule(ctx->device, frag, NULL);
    if (vert) vkDestroyShaderModule(ctx->device, vert, NULL);
    return false;
}

bool fx_make_image_dsl(fx_pipeline_set *ps, fx_context *ctx)
{
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    if (vkCreateDescriptorSetLayout(ctx->device, &ci, NULL, &ps->image_dsl) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateDescriptorSetLayout failed");
        return false;
    }
    return true;
}

bool fx_make_image_pipeline(fx_pipeline_set *ps, fx_context *ctx)
{
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0, .size = sizeof(fx_image_pc),
    };
    VkPipelineLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &ps->image_dsl,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range,
    };
    if (vkCreatePipelineLayout(ctx->device, &lci, NULL, &ps->image_layout) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreatePipelineLayout (image) failed");
        return false;
    }

    VkVertexInputAttributeDescription attrs[2] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(fx_image_vertex, pos) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(fx_image_vertex, uv) },
    };
    return make_pipeline_core(ctx, ps->image_layout, ps->template_render_pass,
                              fx_image_vert_spv, sizeof(fx_image_vert_spv),
                              fx_image_frag_spv, sizeof(fx_image_frag_spv),
                              sizeof(fx_image_vertex), attrs, 2,
                              VK_TRUE,
                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              0xF,
                              VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0x00, 0,
                              &ps->image_pipeline, "image");
}

bool fx_make_gradient_pipeline(fx_pipeline_set *ps, fx_context *ctx)
{
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0, .size = sizeof(fx_gradient_pc),
    };
    VkPipelineLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range,
    };
    if (vkCreatePipelineLayout(ctx->device, &lci, NULL, &ps->gradient_layout) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreatePipelineLayout (gradient) failed");
        return false;
    }

    VkVertexInputAttributeDescription attr = {
        .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0,
    };
    return make_pipeline_core(ctx, ps->gradient_layout, ps->template_render_pass,
                              fx_gradient_vert_spv, sizeof(fx_gradient_vert_spv),
                              fx_gradient_frag_spv, sizeof(fx_gradient_frag_spv),
                              sizeof(fx_solid_vertex), &attr, 1,
                              VK_TRUE,
                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              0xF,
                              VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0x00, 0,
                              &ps->gradient_pipeline, "gradient");
}

bool fx_make_text_pipeline(fx_pipeline_set *ps, fx_context *ctx)
{
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0, .size = sizeof(fx_text_pc),
    };
    VkPipelineLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &ps->image_dsl,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range,
    };
    if (vkCreatePipelineLayout(ctx->device, &lci, NULL, &ps->text_layout) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreatePipelineLayout (text) failed");
        return false;
    }

    VkVertexInputAttributeDescription attrs[2] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(fx_image_vertex, pos) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(fx_image_vertex, uv) },
    };
    return make_pipeline_core(ctx, ps->text_layout, ps->template_render_pass,
                              fx_image_vert_spv, sizeof(fx_image_vert_spv),
                              fx_text_frag_spv, sizeof(fx_text_frag_spv),
                              sizeof(fx_image_vertex), attrs, 2,
                              VK_TRUE,
                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              0xF,
                              VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0x00, 0,
                              &ps->text_pipeline, "text");
}

bool fx_make_solid_pipeline(fx_pipeline_set *ps, fx_context *ctx)
{
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0, .size = sizeof(fx_solid_color_pc),
    };
    VkPipelineLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range,
    };
    if (vkCreatePipelineLayout(ctx->device, &lci, NULL, &ps->solid_rect_layout) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreatePipelineLayout (solid) failed");
        return false;
    }

    VkVertexInputAttributeDescription attr = {
        .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0,
    };
    bool ok = make_pipeline_core(ctx, ps->solid_rect_layout, ps->template_render_pass,
                                 fx_solid_color_vert_spv, sizeof(fx_solid_color_vert_spv),
                                 fx_solid_color_frag_spv, sizeof(fx_solid_color_frag_spv),
                                 sizeof(fx_solid_vertex), &attr, 1,
                                 VK_TRUE,
                                 VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                 VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                 0xF,
                                 VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0x00, 0,
                                 &ps->solid_rect_pipeline, "solid");
    if (!ok) {
        vkDestroyPipelineLayout(ctx->device, ps->solid_rect_layout, NULL);
        ps->solid_rect_layout = VK_NULL_HANDLE;
    }
    return ok;
}

bool fx_make_blur_pipeline(fx_pipeline_set *ps, fx_context *ctx)
{
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0, .size = sizeof(float[2]), /* texel_size vec2 */
    };
    VkPipelineLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &ps->image_dsl,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range,
    };
    if (vkCreatePipelineLayout(ctx->device, &lci, NULL, &ps->blur_layout) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreatePipelineLayout (blur) failed");
        return false;
    }

    VkVertexInputAttributeDescription attrs[2] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(fx_image_vertex, pos) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(fx_image_vertex, uv) },
    };
    return make_pipeline_core(ctx, ps->blur_layout, ps->template_render_pass,
                              fx_image_vert_spv, sizeof(fx_image_vert_spv),
                              fx_blur_frag_spv, sizeof(fx_blur_frag_spv),
                              sizeof(fx_image_vertex), attrs, 2,
                              VK_TRUE,
                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              0xF,
                              VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0x00, 0,
                              &ps->blur_pipeline, "blur");
}

bool fx_make_stencil_pipeline(fx_pipeline_set *ps, fx_context *ctx)
{
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0, .size = sizeof(fx_solid_color_pc),
    };
    VkPipelineLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range,
    };
    if (vkCreatePipelineLayout(ctx->device, &lci, NULL, &ps->stencil_layout) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreatePipelineLayout (stencil) failed");
        return false;
    }

    VkVertexInputAttributeDescription attr = {
        .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0,
    };
    bool ok = make_pipeline_core(ctx, ps->stencil_layout, ps->template_render_pass,
                                 fx_solid_color_vert_spv, sizeof(fx_solid_color_vert_spv),
                                 fx_stencil_frag_spv, sizeof(fx_stencil_frag_spv),
                                 sizeof(fx_solid_vertex), &attr, 1,
                                 VK_TRUE,
                                 VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE,
                                 VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE,
                                 0xF,
                                 VK_STENCIL_OP_REPLACE, VK_COMPARE_OP_ALWAYS, 0xFF, 1,
                                 &ps->stencil_pipeline, "stencil");
    if (!ok) {
        vkDestroyPipelineLayout(ctx->device, ps->stencil_layout, NULL);
        ps->stencil_layout = VK_NULL_HANDLE;
    }
    return ok;
}

bool fx_make_fill_stencil_pipeline(fx_pipeline_set *ps, fx_context *ctx)
{
    VkVertexInputAttributeDescription attr = {
        .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0,
    };
    return make_pipeline_core(ctx, ps->stencil_layout, ps->template_render_pass,
                              fx_solid_color_vert_spv, sizeof(fx_solid_color_vert_spv),
                              fx_stencil_frag_spv, sizeof(fx_stencil_frag_spv),
                              sizeof(fx_solid_vertex), &attr, 1,
                              VK_FALSE,
                              0, 0, 0, 0,
                              0,
                              VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_COMPARE_OP_ALWAYS, 0xFF, 0,
                              &ps->fill_stencil_pipeline, "fill_stencil");
}

bool fx_make_solid_cover_pipeline(fx_pipeline_set *ps, fx_context *ctx)
{
    VkVertexInputAttributeDescription attr = {
        .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0,
    };
    return make_pipeline_core(ctx, ps->solid_rect_layout, ps->template_render_pass,
                              fx_solid_color_vert_spv, sizeof(fx_solid_color_vert_spv),
                              fx_solid_color_frag_spv, sizeof(fx_solid_color_frag_spv),
                              sizeof(fx_solid_vertex), &attr, 1,
                              VK_TRUE,
                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              0xF,
                              VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0x00, 1,
                              &ps->solid_cover_pipeline, "solid_cover");
}

bool fx_make_gradient_cover_pipeline(fx_pipeline_set *ps, fx_context *ctx)
{
    VkVertexInputAttributeDescription attr = {
        .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0,
    };
    return make_pipeline_core(ctx, ps->gradient_layout, ps->template_render_pass,
                              fx_gradient_vert_spv, sizeof(fx_gradient_vert_spv),
                              fx_gradient_frag_spv, sizeof(fx_gradient_frag_spv),
                              sizeof(fx_solid_vertex), &attr, 1,
                              VK_TRUE,
                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              0xF,
                              VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0x00, 1,
                              &ps->gradient_cover_pipeline, "gradient_cover");
}

bool fx_make_images(fx_surface *s)
{
    vkGetSwapchainImagesKHR(s->ctx->device, s->swapchain, &s->image_count, NULL);
    if (s->image_count > FX_MAX_SWAPCHAIN_IMAGES) {
        FX_LOGE(s->ctx, "swapchain image count %u exceeds cap %u",
                s->image_count, FX_MAX_SWAPCHAIN_IMAGES);
        return false;
    }
    VkImage images[FX_MAX_SWAPCHAIN_IMAGES];
    vkGetSwapchainImagesKHR(s->ctx->device, s->swapchain, &s->image_count,
                            images);

    for (uint32_t i = 0; i < s->image_count; ++i) {
        s->images[i].image = images[i];

        VkImageViewCreateInfo vci = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = s->surface_format.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        if (vkCreateImageView(s->ctx->device, &vci, NULL,
                              &s->images[i].view) != VK_SUCCESS) {
            FX_LOGE(s->ctx, "vkCreateImageView failed");
            return false;
        }

        VkImageView fb_attachments[2] = { s->images[i].view, s->stencil_view };
        VkFramebufferCreateInfo fci = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = s->render_pass,
            .attachmentCount = 2,
            .pAttachments    = fb_attachments,
            .width           = s->extent.width,
            .height          = s->extent.height,
            .layers          = 1,
        };
        if (vkCreateFramebuffer(s->ctx->device, &fci, NULL,
                                &s->images[i].framebuffer) != VK_SUCCESS) {
            FX_LOGE(s->ctx, "vkCreateFramebuffer failed");
            return false;
        }
    }
    return true;
}

bool fx_make_frames(fx_surface *s)
{
    VkCommandBufferAllocateInfo ai = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = s->ctx->frame_cmd_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fci = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    VkSamplerCreateInfo sci_sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (vkCreateSampler(s->ctx->device, &sci_sampler, NULL, &s->sampler) != VK_SUCCESS) return false;

    for (uint32_t i = 0; i < FX_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkAllocateCommandBuffers(s->ctx->device, &ai,
                                     &s->frames[i].cmd) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(s->ctx->device, &sci, NULL,
                              &s->frames[i].image_available) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(s->ctx->device, &sci, NULL,
                              &s->frames[i].render_finished) != VK_SUCCESS) return false;
        if (vkCreateFence(s->ctx->device, &fci, NULL,
                          &s->frames[i].in_flight) != VK_SUCCESS) return false;

        fx_vbuf_pool_init(&s->frames[i].vbuf, s->ctx);

        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
        };
        VkDescriptorPoolCreateInfo pci_desc = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 64,
            .poolSizeCount = 1,
            .pPoolSizes = pool_sizes,
        };
        if (vkCreateDescriptorPool(s->ctx->device, &pci_desc, NULL, &s->frames[i].desc_pool) != VK_SUCCESS) return false;
    }
    return true;
}

bool fx_swapchain_build(fx_surface *s)
{
    VkPhysicalDevice phys = s->ctx->phys;
    VkDevice         dev  = s->ctx->device;

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, s->vk_surface, &caps);

    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, s->vk_surface, &nf, NULL);
    VkSurfaceFormatKHR *fmts = calloc(nf, sizeof(*fmts));
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, s->vk_surface, &nf, fmts);

    uint32_t np = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, s->vk_surface, &np, NULL);
    VkPresentModeKHR *pms = calloc(np, sizeof(*pms));
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, s->vk_surface, &np, pms);

    s->surface_format = pick_format(fmts, nf);
    s->present_mode   = pick_present_mode(pms, np);
    s->extent         = clamp_extent(caps, s->requested_w, s->requested_h);

    free(fmts);
    free(pms);

    if (s->extent.width == 0 || s->extent.height == 0) {
        /* Minimized / hidden: skip building, retry on resize. */
        s->needs_recreate = true;
        return true;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;
    if (image_count > FX_MAX_SWAPCHAIN_IMAGES)
        image_count = FX_MAX_SWAPCHAIN_IMAGES;

    VkSwapchainCreateInfoKHR ci = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = s->vk_surface,
        .minImageCount    = image_count,
        .imageFormat      = s->surface_format.format,
        .imageColorSpace  = s->surface_format.colorSpace,
        .imageExtent      = s->extent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                          | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = s->present_mode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = VK_NULL_HANDLE,
    };
    /* Some compositors don't support OPAQUE; pick the first supported. */
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
            ci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
            ci.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
            ci.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }

    VkResult r = vkCreateSwapchainKHR(dev, &ci, NULL, &s->swapchain);
    if (r != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkCreateSwapchainKHR: %d", (int)r);
        return false;
    }

    /* Create stencil attachment */
    VkImageCreateInfo sici = {
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = VK_FORMAT_S8_UINT,
        .extent      = { s->extent.width, s->extent.height, 1 },
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo stencil_aci = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };
    r = vmaCreateImage(s->ctx->vma_allocator, &sici, &stencil_aci,
                       &s->stencil_image, &s->stencil_alloc, NULL);
    if (r != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vmaCreateImage (stencil): %d", (int)r);
        return false;
    }

    VkImageViewCreateInfo svci = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = s->stencil_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = VK_FORMAT_S8_UINT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    r = vkCreateImageView(dev, &svci, NULL, &s->stencil_view);
    if (r != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkCreateImageView (stencil): %d", (int)r);
        return false;
    }

    /* Render pass is surface-lifetime; pipelines are context-shared. */
    if (!s->render_pass) {
        if (!fx_make_render_pass(s, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)) return false;
        s->pc = fx_pipeline_set_get(s->ctx, s->surface_format.format, VK_SAMPLE_COUNT_1_BIT);
        if (!s->pc) return false;
    }
    if (!fx_make_images(s))      return false;
    if (!fx_make_frames(s))      return false;

    FX_LOGI(s->ctx, "swapchain %ux%u images=%u format=%d present=%d",
            s->extent.width, s->extent.height, s->image_count,
            (int)s->surface_format.format, (int)s->present_mode);
    s->needs_recreate = false;
    return true;
}

/* Destroys only the swapchain-lifetime resources: swapchain, swapchain images
 * and their framebuffers, the stencil attachment, and the per-frame sync +
 * command/descriptor pools. Render pass, pipelines, samplers, and DSL are
 * surface-lifetime and are torn down by fx_surface_destroy_pipelines. */
void fx_swapchain_destroy(fx_surface *s)
{
    VkDevice dev = s->ctx->device;

    for (uint32_t i = 0; i < FX_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (s->frames[i].in_flight) {
            vkDestroyFence(dev, s->frames[i].in_flight, NULL);
            s->frames[i].in_flight = VK_NULL_HANDLE;
        }
        if (s->frames[i].image_available) {
            vkDestroySemaphore(dev, s->frames[i].image_available, NULL);
            s->frames[i].image_available = VK_NULL_HANDLE;
        }
        if (s->frames[i].render_finished) {
            vkDestroySemaphore(dev, s->frames[i].render_finished, NULL);
            s->frames[i].render_finished = VK_NULL_HANDLE;
        }
        if (s->frames[i].cmd) {
            vkFreeCommandBuffers(dev, s->ctx->frame_cmd_pool, 1,
                                 &s->frames[i].cmd);
            s->frames[i].cmd = VK_NULL_HANDLE;
        }
        fx_vbuf_pool_destroy(&s->frames[i].vbuf);
        fx_arena_destroy(&s->frames[i].arena);
        if (s->frames[i].desc_pool) {
            vkDestroyDescriptorPool(dev, s->frames[i].desc_pool, NULL);
            s->frames[i].desc_pool = VK_NULL_HANDLE;
        }
    }
    for (uint32_t i = 0; i < s->image_count; ++i) {
        if (s->images[i].framebuffer) {
            vkDestroyFramebuffer(dev, s->images[i].framebuffer, NULL);
            s->images[i].framebuffer = VK_NULL_HANDLE;
        }
        if (s->images[i].view) {
            vkDestroyImageView(dev, s->images[i].view, NULL);
            s->images[i].view = VK_NULL_HANDLE;
        }
        s->images[i].image = VK_NULL_HANDLE;
    }
    s->image_count = 0;

    if (s->stencil_view) {
        vkDestroyImageView(dev, s->stencil_view, NULL);
        s->stencil_view = VK_NULL_HANDLE;
    }
    if (s->stencil_image) {
        vmaDestroyImage(s->ctx->vma_allocator, s->stencil_image, s->stencil_alloc);
        s->stencil_image = VK_NULL_HANDLE;
        s->stencil_alloc = VK_NULL_HANDLE;
    }
    if (s->swapchain) {
        vkDestroySwapchainKHR(dev, s->swapchain, NULL);
        s->swapchain = VK_NULL_HANDLE;
    }
}

/* Tears down the surface-lifetime state (render pass, samplers).
 * Pipelines are context-shared and destroyed with the context. */
void fx_surface_destroy_pipelines(fx_surface *s)
{
    VkDevice dev = s->ctx->device;

    if (s->sampler) {
        vkDestroySampler(dev, s->sampler, NULL);
        s->sampler = VK_NULL_HANDLE;
    }
    if (s->render_pass) {
        vkDestroyRenderPass(dev, s->render_pass, NULL);
        s->render_pass = VK_NULL_HANDLE;
    }
}

void fx_surface_wait_idle(fx_surface *s)
{
    if (!s->ctx || !s->ctx->device) return;
    VkFence fences[FX_MAX_FRAMES_IN_FLIGHT];
    uint32_t n = 0;
    for (uint32_t i = 0; i < FX_MAX_FRAMES_IN_FLIGHT; ++i)
        if (s->frames[i].in_flight) fences[n++] = s->frames[i].in_flight;
    if (n) vkWaitForFences(s->ctx->device, n, fences, VK_TRUE, UINT64_MAX);
}
