/* Internal declarations shared across flux translation units. */
#ifndef FX_INTERNAL_H
#define FX_INTERNAL_H

#include "flux/flux.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>

#include "vk/memory.h"

#define FX_MAX_FRAMES_IN_FLIGHT 2
#define FX_MAX_SWAPCHAIN_IMAGES 8

/* ---------- memory management ---------- */

typedef struct fx_arena_block {
    struct fx_arena_block *next;
    size_t size;
    size_t used;
    uint8_t data[];
} fx_arena_block;

typedef struct {
    fx_arena_block *head;
    size_t block_size;
} fx_arena;

FX_API void fx_arena_init(fx_arena *arena, size_t block_size);
FX_API void fx_arena_destroy(fx_arena *arena);
FX_API void *fx_arena_alloc(fx_arena *arena, size_t size);
FX_API void fx_arena_reset(fx_arena *arena);

/* ---------- logging ---------- */

void fx_log(const fx_context *ctx, fx_log_level lvl,
            const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#define FX_LOGE(ctx, ...) fx_log((ctx), FX_LOG_ERROR, __VA_ARGS__)
#define FX_LOGW(ctx, ...) fx_log((ctx), FX_LOG_WARN,  __VA_ARGS__)
#define FX_LOGI(ctx, ...) fx_log((ctx), FX_LOG_INFO,  __VA_ARGS__)
#define FX_LOGD(ctx, ...) fx_log((ctx), FX_LOG_DEBUG, __VA_ARGS__)

#define FX_CHECK_VK(ctx, expr) \
    do { \
        VkResult _r = (expr); \
        if (_r != VK_SUCCESS) { \
            FX_LOGE((ctx), "%s:%d: %s => VkResult %d", \
                    __FILE__, __LINE__, #expr, (int)_r); \
        } \
    } while (0)

/* ---------- glyph atlas ---------- */

typedef struct {
    uint32_t glyph_id;
    void    *font_id;   /* fx_font pointer */
    float    u0, v0, u1, v1;
    int      w, h;
    int      bearing_x, bearing_y;
    int      advance;
} fx_atlas_entry;

struct fx_atlas {
    struct fx_image *image;
    fx_atlas_entry  *entries;
    size_t           entry_count;
    size_t           entry_cap;

    int              shelf_y;
    int              shelf_h;
    int              shelf_x;
};

/* ---------- fx_context ---------- */

struct fx_context {
    fx_log_fn log;
    void     *log_user;

    VkInstance                 instance;
    VkDebugUtilsMessengerEXT   debug_messenger;
    bool                       validation_enabled;

    VkPhysicalDevice           phys;
    VkPhysicalDeviceProperties phys_props;
    VkPhysicalDeviceMemoryProperties mem_props;
    bool                       queue_supports_present;

    VkDevice      device;
    uint32_t      graphics_family;
    VkQueue       graphics_queue;

    VkCommandPool frame_cmd_pool;
    FT_Library    ft_lib;

    struct fx_atlas atlas;
};

bool fx_instance_create(fx_context *ctx, const char *app_name,
                        bool want_validation,
                        const char *const *exts_wanted,
                        uint32_t exts_wanted_n);
bool fx_device_init(fx_context *ctx, VkSurfaceKHR probe_surface);
void fx_device_shutdown(fx_context *ctx);
void fx_instance_destroy(fx_context *ctx);

/* ---------- fx_font ---------- */

struct fx_path {
    uint8_t  *verbs;
    size_t    verb_count;
    size_t    verb_cap;
    fx_point *points;
    size_t    point_count;
    size_t    point_cap;
    fx_rect   bounds;
    bool      has_bounds;

    /* Geometry Cache */
    uint32_t  generation;

    uint32_t  fill_generation;
    fx_point *fill_tris;
    size_t    fill_tri_count;

    uint32_t  stroke_generation;
    float     stroke_width;
    uint32_t  stroke_join;
    uint32_t  stroke_cap;
    float     stroke_miter_limit;
    fx_point *stroke_tris;
    size_t    stroke_tri_count;
};

struct fx_glyph_run {
    fx_glyph *glyphs;
    size_t    count;
    size_t    cap;
};

struct fx_gradient {
    fx_context *ctx;
    uint32_t    mode;   /* 0 = linear, 1 = radial */
    float       start[2];
    float       end[2];
    float       colors[4][4];
    float       stops[4];
    uint32_t    stop_count;
};

struct fx_font {
    fx_context *ctx;
    char   *family;
    char   *source_name;
    float   size;
    int32_t weight;
    bool    italic;
    FT_Face      ft_face;
    hb_font_t   *hb_font;
    float   ascender;    /* pixels, from baseline to top */
    float   descender;   /* pixels, from baseline to bottom (negative) */
};

/* ---------- fx_image ---------- */

struct fx_image {
    fx_context    *ctx;
    fx_image_desc  desc;
    void          *data;
    size_t         data_size;

    VkImage        vk_image;
    VkImageView    vk_view;
    VkDeviceMemory vk_mem;
};

/* ---------- fx_surface ---------- */

typedef struct {
    VkImage       image;
    VkImageView   view;
    VkFramebuffer framebuffer;
} fx_sc_image;

typedef struct {
    float surface_size[2];
    uint32_t mode; float pad;
    float color[4];
} fx_solid_color_pc;

typedef struct {
    float surface_size[2];
    uint32_t mode; float pad;
    float color[4];
} fx_text_pc;

typedef struct {
    float surface_size[2];
    uint32_t mode; float pad;
} fx_image_pc;

/* Gradient push constants: 112 bytes total */
typedef struct {
    float    surface_size[2];
    uint32_t mode;
    uint32_t stop_count;
    float    start[2];
    float    end[2];
    float    colors[4][4];
    float    stops[4];
} fx_gradient_pc;

typedef struct {
    float pos[2];
    float uv[2];
} fx_image_vertex;

typedef struct {
    float pos[2];
} fx_solid_vertex;

typedef struct {
    VkSemaphore     image_available;
    VkSemaphore     render_finished;
    VkFence         in_flight;
    VkCommandBuffer cmd;
    fx_vbuf_pool    vbuf;
    VkDescriptorPool desc_pool;
    fx_arena        arena;
} fx_frame;

typedef enum {
    FX_OP_FILL_PATH = 0,
    FX_OP_STROKE_PATH = 1,
    FX_OP_DRAW_IMAGE = 2,
    FX_OP_DRAW_GLYPHS = 3,
    FX_OP_CLIP_RECT = 4,
    FX_OP_RESET_CLIP = 5,
    FX_OP_CLIP_PATH = 6,
} fx_op_kind;

typedef struct {
    const fx_path *path;
    fx_paint       paint;
    bool           owns_path;
} fx_fill_path_op;

typedef struct {
    const fx_path *path;
    fx_paint       paint;
    bool           owns_path;
} fx_stroke_path_op;

typedef struct {
    const fx_image *image;
    fx_rect         src;
    fx_rect         dst;
uint32_t        mode;
} fx_draw_image_op;

typedef struct {
    const fx_font      *font;
    const fx_glyph_run *run;
    float               x;
    float               y;
    fx_paint            paint;
} fx_draw_glyphs_op;

typedef struct {
    fx_rect rect;
} fx_clip_rect_op;

typedef struct {
    fx_op_kind kind;
    union {
        fx_fill_path_op    fill_path;
        fx_stroke_path_op  stroke_path;
        fx_draw_image_op   draw_image;
        fx_draw_glyphs_op  draw_glyphs;
        fx_clip_rect_op    clip_rect;
        fx_fill_path_op    clip_path;  /* reuses fill_path shape, paint ignored */
    } u;
} fx_op;

struct fx_surface {
    fx_context *ctx;

    VkSurfaceKHR      vk_surface;
    VkSwapchainKHR    swapchain;

    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR   present_mode;
    VkExtent2D         extent;

    VkRenderPass      render_pass;
    VkPipelineLayout  solid_rect_layout;
    VkPipeline        solid_rect_pipeline;

    VkDescriptorSetLayout image_dsl;
    VkPipelineLayout  image_layout;
    VkPipeline        image_pipeline;

    VkPipelineLayout  text_layout;
    VkPipeline        text_pipeline;

    VkPipelineLayout  gradient_layout;
    VkPipeline        gradient_pipeline;

    VkPipelineLayout  stencil_layout;
    VkPipeline        stencil_pipeline;

    VkPipelineLayout  blur_layout;
    VkPipeline        blur_pipeline;

    VkSampler         sampler;
    VkSampler         text_sampler;


    fx_sc_image       images[FX_MAX_SWAPCHAIN_IMAGES];
    uint32_t          image_count;

    /* Stencil attachment for clipping */
    VkImage           stencil_image;
    VkDeviceMemory    stencil_memory;
    VkImageView       stencil_view;

    fx_frame          frames[FX_MAX_FRAMES_IN_FLIGHT];
    uint32_t          frame_index;      /* cycles 0..MAX_FRAMES-1 */
    uint32_t          acquired_image;   /* set by acquire, read by present */

    bool              needs_recreate;
    int32_t           requested_w, requested_h;


    fx_color_space    color_space;

    /* Offscreen rendering state (no swapchain, no VkSurfaceKHR). */
    bool              is_offscreen;
    VkImage           offscreen_image;
    VkDeviceMemory    offscreen_memory;
    VkImageView       offscreen_view;
    VkFramebuffer     offscreen_framebuffer;


    /* Canvas recording state: commands are appended CPU-side. */
    struct fx_canvas {
        fx_surface *owner;
        fx_color    clear_color;
        bool        has_clear;
        fx_op      *ops;
        size_t      op_count;
        size_t      op_cap;

        fx_matrix  *state_stack;
        size_t      state_count;
        size_t      state_cap;
        fx_matrix   current_matrix;
        float       dpr;   /* device pixel ratio */
    } canvas;
};

uint32_t find_memory_type(fx_context *ctx, uint32_t type_filter,
                          VkMemoryPropertyFlags props);
bool fx_swapchain_build(fx_surface *s);
void fx_swapchain_destroy(fx_surface *s);
/* Waits on all in-flight frame fences and destroys per-frame objects. */
void fx_surface_wait_idle(fx_surface *s);

/* Internal pipeline/frame helpers used by both swapchain and offscreen paths. */
bool fx_make_render_pass(fx_surface *s, VkImageLayout final_layout);
bool fx_make_image_dsl(fx_surface *s);
bool fx_make_image_pipeline(fx_surface *s);
bool fx_make_text_pipeline(fx_surface *s);
bool fx_make_gradient_pipeline(fx_surface *s);
bool fx_make_blur_pipeline(fx_surface *s);
bool fx_make_stencil_pipeline(fx_surface *s);
bool fx_make_bootstrap_pipeline(fx_surface *s);
bool fx_make_frames(fx_surface *s);
void fx_canvas_reset(fx_canvas *c);
void fx_canvas_dispose(fx_canvas *c);
bool fx_path_is_axis_aligned_rect(const fx_path *path, fx_rect *out_rect);
bool fx_path_get_line_loop(const fx_path *path,
                           const fx_point **out_points,
                           size_t *out_count);
FX_API bool fx_path_flatten_polyline(const fx_path *path, float tolerance,
                                       fx_arena *arena,
                                       fx_point **out_points, size_t *out_count,
                                       bool *out_closed);
FX_API bool fx_path_flatten_line_loop(const fx_path *path, float tolerance,
                                        fx_arena *arena,
                                        fx_point **out_points, size_t *out_count);
FX_API bool fx_tessellate_simple_polygon(const fx_point *points, size_t count,
                                           fx_arena *arena,
                                           fx_point **out_tris, size_t *out_count);
FX_API bool fx_stroke_polyline(const fx_point *points, size_t count, bool closed,
                                 const fx_paint *paint, fx_arena *arena,
                                 fx_point **out_tris, size_t *out_count);

bool fx_atlas_ensure_glyph(fx_context *ctx, fx_font *font, uint32_t glyph_id, fx_atlas_entry *out_entry);

/* ---------- matrix & path transform ---------- */

static inline void fx_matrix_identity(fx_matrix *m)
{
    m->m[0] = 1.0f; m->m[1] = 0.0f;
    m->m[2] = 0.0f; m->m[3] = 1.0f;
    m->m[4] = 0.0f; m->m[5] = 0.0f;
}

static inline bool fx_matrix_is_identity(const fx_matrix *m)
{
    return m->m[0] == 1.0f && m->m[1] == 0.0f &&
           m->m[2] == 0.0f && m->m[3] == 1.0f &&
           m->m[4] == 0.0f && m->m[5] == 0.0f;
}

#endif /* FX_INTERNAL_H */
