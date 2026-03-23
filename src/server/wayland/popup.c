/**
 * @file popup.c
 * @brief Wayland input-popup candidate UI
 */


#include "wl_frontend_internal.h"
#include "preedit_format.h"
#include "typio/config.h"
#include "typio/instance.h"
#include "typio/engine_manager.h"
#include "utils/log.h"

#include <cairo.h>
#include <pango/pangocairo.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define TYPIO_POPUP_BUFFER_COUNT 2
#define TYPIO_POPUP_DEFAULT_FONT_SIZE 11
#define TYPIO_POPUP_PADDING 6
#define TYPIO_POPUP_ROW_GAP 2
#define TYPIO_POPUP_COLUMN_GAP 6
#define TYPIO_POPUP_SECTION_GAP 4
#define TYPIO_POPUP_MIN_WIDTH 120
#define TYPIO_POPUP_MAX_WIDTH 4096

typedef enum TypioPopupThemeMode {
    TYPIO_POPUP_THEME_AUTO = 0,
    TYPIO_POPUP_THEME_LIGHT,
    TYPIO_POPUP_THEME_DARK,
} TypioPopupThemeMode;

typedef enum TypioPopupLayoutMode {
    TYPIO_POPUP_LAYOUT_HORIZONTAL = 0,
    TYPIO_POPUP_LAYOUT_VERTICAL,
} TypioPopupLayoutMode;

typedef struct TypioPopupPalette {
    double bg_r, bg_g, bg_b, bg_a;
    double border_r, border_g, border_b, border_a;
    double preedit_r, preedit_g, preedit_b;
    double text_r, text_g, text_b;
    double muted_r, muted_g, muted_b;
    double selection_r, selection_g, selection_b, selection_a;
    double selection_text_r, selection_text_g, selection_text_b;
} TypioPopupPalette;

typedef struct TypioPopupRenderConfig {
    TypioPopupThemeMode theme_mode;
    TypioPopupLayoutMode layout_mode;
    int font_size;          /* Candidate font size (e.g. 11) */
    char font_desc[64];     /* "Sans 11" */
    char page_font_desc[64]; /* "Sans 10" — one point smaller for page info */
} TypioPopupRenderConfig;

typedef struct TypioWlPopupBuffer {
    struct wl_buffer *buffer;
    void *data;
    size_t size;
    int width;
    int height;
    int stride;
    bool busy;
} TypioWlPopupBuffer;

typedef struct TypioWlPopupOutputRef {
    struct wl_output *output;
    struct TypioWlPopupOutputRef *next;
} TypioWlPopupOutputRef;

struct TypioWlPopup {
    TypioWlFrontend *frontend;
    struct wl_surface *surface;
    struct zwp_input_popup_surface_v2 *popup_surface;
    TypioWlPopupBuffer buffers[TYPIO_POPUP_BUFFER_COUNT];
    bool visible;
    int text_input_x;
    int text_input_y;
    int text_input_width;
    int text_input_height;
    TypioWlPopupOutputRef *entered_outputs;
};

typedef struct TypioPopupLine {
    char *text;
    bool selected;
    int width;
    int height;
    int x;
    int y;
} TypioPopupLine;

static const TypioPopupPalette popup_palette_light = {
    .bg_r = 0.98, .bg_g = 0.98, .bg_b = 0.98, .bg_a = 0.96,
    .border_r = 0.80, .border_g = 0.82, .border_b = 0.85, .border_a = 1.0,
    .preedit_r = 0.34, .preedit_g = 0.34, .preedit_b = 0.34,
    .text_r = 0.12, .text_g = 0.12, .text_b = 0.12,
    .muted_r = 0.38, .muted_g = 0.38, .muted_b = 0.38,
    .selection_r = 0.14, .selection_g = 0.38, .selection_b = 0.89, .selection_a = 0.95,
    .selection_text_r = 1.0, .selection_text_g = 1.0, .selection_text_b = 1.0,
};

static const TypioPopupPalette popup_palette_dark = {
    .bg_r = 0.11, .bg_g = 0.12, .bg_b = 0.14, .bg_a = 0.97,
    .border_r = 0.28, .border_g = 0.30, .border_b = 0.34, .border_a = 1.0,
    .preedit_r = 0.78, .preedit_g = 0.79, .preedit_b = 0.82,
    .text_r = 0.92, .text_g = 0.93, .text_b = 0.95,
    .muted_r = 0.66, .muted_g = 0.68, .muted_b = 0.72,
    .selection_r = 0.25, .selection_g = 0.47, .selection_b = 0.96, .selection_a = 0.95,
    .selection_text_r = 1.0, .selection_text_g = 1.0, .selection_text_b = 1.0,
};

static void popup_handle_text_input_rectangle(void *data,
                                              struct zwp_input_popup_surface_v2 *popup_surface,
                                              int32_t x, int32_t y,
                                              int32_t width, int32_t height);
static void popup_handle_surface_enter(void *data, struct wl_surface *surface,
                                       struct wl_output *output);
static void popup_handle_surface_leave(void *data, struct wl_surface *surface,
                                       struct wl_output *output);

static const struct zwp_input_popup_surface_v2_listener popup_listener = {
    .text_input_rectangle = popup_handle_text_input_rectangle,
};

static const struct wl_surface_listener popup_surface_listener = {
    .enter = popup_handle_surface_enter,
    .leave = popup_handle_surface_leave,
};

static void popup_buffer_release(void *data, [[maybe_unused]] struct wl_buffer *buffer) {
    TypioWlPopupBuffer *popup_buffer = data;
    popup_buffer->busy = false;
}

static const struct wl_buffer_listener popup_buffer_listener = {
    .release = popup_buffer_release,
};

static void popup_buffer_reset(TypioWlPopupBuffer *buffer) {
    if (!buffer) {
        return;
    }

    if (buffer->buffer) {
        wl_buffer_destroy(buffer->buffer);
    }
    if (buffer->data && buffer->size > 0) {
        munmap(buffer->data, buffer->size);
    }

    memset(buffer, 0, sizeof(*buffer));
}

static int create_shm_file(size_t size) {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    char template_path[512];
    int fd;

    snprintf(template_path, sizeof(template_path), "%s/%s",
             (runtime_dir && runtime_dir[0]) ? runtime_dir : "/tmp",
             "typio-popup-XXXXXX");

    fd = mkstemp(template_path);
    if (fd < 0) {
        return -1;
    }

    unlink(template_path);

    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static bool popup_buffer_create(TypioWlPopup *popup, TypioWlPopupBuffer *buffer,
                                int width, int height) {
    int stride;
    size_t size;
    int fd;
    void *data;
    struct wl_shm_pool *pool;

    popup_buffer_reset(buffer);

    stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    size = (size_t)stride * (size_t)height;
    fd = create_shm_file(size);
    if (fd < 0) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create popup shm file: %s",
                  strerror(errno));
        return false;
    }

    data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        typio_log(TYPIO_LOG_ERROR, "Failed to mmap popup buffer: %s",
                  strerror(errno));
        close(fd);
        return false;
    }

    pool = wl_shm_create_pool(popup->frontend->shm, fd, (int32_t)size);
    close(fd);
    if (!pool) {
        munmap(data, size);
        return false;
    }

    buffer->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                                               WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    if (!buffer->buffer) {
        munmap(data, size);
        return false;
    }

    buffer->data = data;
    buffer->size = size;
    buffer->width = width;
    buffer->height = height;
    buffer->stride = stride;
    buffer->busy = false;
    wl_buffer_add_listener(buffer->buffer, &popup_buffer_listener, buffer);
    return true;
}

static TypioWlPopupBuffer *popup_acquire_buffer(TypioWlPopup *popup, int width, int height) {
    for (size_t i = 0; i < TYPIO_POPUP_BUFFER_COUNT; ++i) {
        TypioWlPopupBuffer *buffer = &popup->buffers[i];

        if (buffer->busy) {
            continue;
        }

        if (buffer->buffer && buffer->width == width && buffer->height == height) {
            return buffer;
        }

        if (popup_buffer_create(popup, buffer, width, height)) {
            return buffer;
        }
    }

    typio_log(TYPIO_LOG_WARNING, "No free popup buffer available");
    return nullptr;
}

static const TypioWlOutput *popup_find_frontend_output(const TypioWlPopup *popup,
                                                        struct wl_output *output) {
    for (TypioWlOutput *current = popup && popup->frontend ? popup->frontend->outputs : nullptr;
         current;
         current = current->next) {
        if (current->output == output) {
            return current;
        }
    }

    return nullptr;
}

static bool popup_tracks_output(const TypioWlPopup *popup, struct wl_output *output) {
    for (TypioWlPopupOutputRef *entry = popup ? popup->entered_outputs : nullptr;
         entry;
         entry = entry->next) {
        if (entry->output == output) {
            return true;
        }
    }

    return false;
}

static int popup_render_scale(const TypioWlPopup *popup) {
    int scale = 1;

    for (TypioWlPopupOutputRef *entry = popup ? popup->entered_outputs : nullptr;
         entry;
         entry = entry->next) {
        const TypioWlOutput *output = popup_find_frontend_output(popup, entry->output);
        if (output && output->scale > scale) {
            scale = output->scale;
        }
    }

    return scale;
}

static void popup_refresh_visible_surface(TypioWlPopup *popup) {
    TypioInputContext *ctx;

    if (!popup || !popup->visible || !popup->frontend || !popup->frontend->session) {
        return;
    }

    ctx = popup->frontend->session->ctx;
    if (!ctx) {
        return;
    }

    typio_wl_popup_update(popup->frontend, ctx);
}

static void popup_track_output(TypioWlPopup *popup, struct wl_output *output) {
    TypioWlPopupOutputRef *entry;

    if (!popup || !output || popup_tracks_output(popup, output)) {
        return;
    }

    entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return;
    }

    entry->output = output;
    entry->next = popup->entered_outputs;
    popup->entered_outputs = entry;
    popup_refresh_visible_surface(popup);
}

static void popup_untrack_output(TypioWlPopup *popup, struct wl_output *output) {
    TypioWlPopupOutputRef **link;

    if (!popup || !output) {
        return;
    }

    link = &popup->entered_outputs;
    while (*link) {
        TypioWlPopupOutputRef *entry = *link;
        if (entry->output == output) {
            *link = entry->next;
            free(entry);
            popup_refresh_visible_surface(popup);
            return;
        }
        link = &entry->next;
    }
}

static void popup_clear_outputs(TypioWlPopup *popup) {
    while (popup && popup->entered_outputs) {
        TypioWlPopupOutputRef *entry = popup->entered_outputs;
        popup->entered_outputs = entry->next;
        free(entry);
    }
}

static bool popup_scaled_dimension(int logical, int scale, int *physical) {
    if (!physical || logical < 0 || scale < 1) {
        return false;
    }

    if (logical > INT32_MAX / scale) {
        return false;
    }

    *physical = logical * scale;
    return true;
}

static char *popup_format_candidate(const TypioCandidate *candidate, size_t index) {
    const char *label;
    const char *text;
    const char *comment;
    char fallback_label[32];
    size_t needed;
    char *formatted;

    if (candidate && candidate->label && candidate->label[0]) {
        label = candidate->label;
    } else {
        snprintf(fallback_label, sizeof(fallback_label), "%zu", index + 1);
        label = fallback_label;
    }

    text = (candidate && candidate->text) ? candidate->text : "";
    comment = (candidate && candidate->comment) ? candidate->comment : "";

    needed = strlen(label) + strlen(text) + strlen(comment) + 6;
    formatted = calloc(needed, sizeof(char));
    if (!formatted) {
        return nullptr;
    }

    if (comment[0]) {
        snprintf(formatted, needed, "%s. %s  %s", label, text, comment);
    } else {
        snprintf(formatted, needed, "%s. %s", label, text);
    }

    return formatted;
}

static void popup_free_lines(TypioPopupLine *lines, size_t count) {
    if (!lines) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        free(lines[i].text);
    }
    free(lines);
}

static bool popup_line_is_dark_env(const char *value) {
    if (!value || !*value) {
        return false;
    }

    return strstr(value, "dark") != nullptr || strstr(value, "Dark") != nullptr;
}

static bool popup_config_file_prefers_dark(const char *path, const char *needle) {
    FILE *file;
    char line[512];

    if (!path || !needle) {
        return false;
    }

    file = fopen(path, "r");
    if (!file) {
        return false;
    }

    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, needle) != nullptr) {
            fclose(file);
            return true;
        }
    }

    fclose(file);
    return false;
}

static bool popup_kde_prefers_dark(void) {
    const char *home = getenv("HOME");
    char path[512];
    FILE *file;
    char line[512];
    bool in_general = false;

    if (!home || !*home) {
        return false;
    }

    snprintf(path, sizeof(path), "%s/.config/kdeglobals", home);
    file = fopen(path, "r");
    if (!file) {
        return false;
    }

    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '[') {
            in_general = strstr(line, "[General]") != nullptr;
            continue;
        }

        if (in_general && strncmp(line, "ColorScheme=", 12) == 0) {
            char *value = line + 12;
            while (*value == ' ' || *value == '\t') {
                ++value;
            }
            fclose(file);
            return popup_line_is_dark_env(value);
        }
    }

    fclose(file);
    return false;
}

static bool popup_prefers_dark_theme(void) {
    const char *home = getenv("HOME");
    const char *gtk_theme = getenv("GTK_THEME");
    char path[512];

    if (popup_line_is_dark_env(gtk_theme)) {
        return true;
    }

    if (!home || !*home) {
        return false;
    }

    snprintf(path, sizeof(path), "%s/.config/gtk-4.0/settings.ini", home);
    if (popup_config_file_prefers_dark(path, "gtk-application-prefer-dark-theme=1")) {
        return true;
    }

    snprintf(path, sizeof(path), "%s/.config/gtk-3.0/settings.ini", home);
    if (popup_config_file_prefers_dark(path, "gtk-application-prefer-dark-theme=1")) {
        return true;
    }

    return popup_kde_prefers_dark();
}

static const TypioPopupPalette *popup_resolve_palette(TypioPopupThemeMode theme_mode) {
    switch (theme_mode) {
        case TYPIO_POPUP_THEME_DARK:
            return &popup_palette_dark;
        case TYPIO_POPUP_THEME_LIGHT:
            return &popup_palette_light;
        case TYPIO_POPUP_THEME_AUTO:
        default:
            return popup_prefers_dark_theme() ? &popup_palette_dark : &popup_palette_light;
    }
}

static void popup_load_render_config(TypioWlPopup *popup, TypioPopupRenderConfig *config) {
    TypioEngineManager *manager;
    TypioEngine *engine;
    TypioConfig *engine_config;
    const char *theme;
    const char *layout;

    if (!popup || !config) {
        return;
    }

    config->theme_mode = TYPIO_POPUP_THEME_AUTO;
    config->layout_mode = TYPIO_POPUP_LAYOUT_HORIZONTAL;
    config->font_size = TYPIO_POPUP_DEFAULT_FONT_SIZE;

    manager = typio_instance_get_engine_manager(popup->frontend->instance);
    engine = manager ? typio_engine_manager_get_active(manager) : nullptr;
    if (!engine || strcmp(typio_engine_get_name(engine), "rime") != 0) {
        goto finalize;
    }

    engine_config = typio_instance_get_engine_config(popup->frontend->instance, "rime");
    if (!engine_config) {
        goto finalize;
    }

    theme = typio_config_get_string(engine_config, "popup_theme", nullptr);
    layout = typio_config_get_string(engine_config, "candidate_layout", nullptr);

    if (theme && strcmp(theme, "dark") == 0) {
        config->theme_mode = TYPIO_POPUP_THEME_DARK;
    } else if (theme && strcmp(theme, "light") == 0) {
        config->theme_mode = TYPIO_POPUP_THEME_LIGHT;
    }

    if (layout && strcmp(layout, "vertical") == 0) {
        config->layout_mode = TYPIO_POPUP_LAYOUT_VERTICAL;
    }

    config->font_size = typio_config_get_int(engine_config, "font_size",
                                              TYPIO_POPUP_DEFAULT_FONT_SIZE);
    if (config->font_size < 6) {
        config->font_size = 6;
    } else if (config->font_size > 72) {
        config->font_size = 72;
    }

    typio_config_free(engine_config);

finalize:
    snprintf(config->font_desc, sizeof(config->font_desc),
             "Sans %d", config->font_size);
    snprintf(config->page_font_desc, sizeof(config->page_font_desc),
             "Sans %d", config->font_size > 6 ? config->font_size - 1 : 6);
}

static bool popup_measure_text(cairo_t *cr, const char *font_desc,
                               const char *text, int *width, int *height) {
    PangoLayout *layout;
    PangoFontDescription *font;

    layout = pango_cairo_create_layout(cr);
    if (!layout) {
        return false;
    }

    font = pango_font_description_from_string(font_desc);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text ? text : "", -1);
    pango_layout_get_pixel_size(layout, width, height);

    pango_font_description_free(font);
    g_object_unref(layout);
    return true;
}

static void popup_draw_text(cairo_t *cr, const char *font_desc, const char *text,
                            double x, double y, double r, double g, double b) {
    PangoLayout *layout;
    PangoFontDescription *font;

    layout = pango_cairo_create_layout(cr);
    font = pango_font_description_from_string(font_desc);

    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text ? text : "", -1);

    cairo_move_to(cr, x, y);
    cairo_set_source_rgb(cr, r, g, b);
    pango_cairo_show_layout(cr, layout);

    pango_font_description_free(font);
    g_object_unref(layout);
}

static void popup_draw_candidate_row(cairo_t *cr, int x, int y, int width, int height,
                                     const char *text, bool selected,
                                     const char *font, const TypioPopupPalette *palette) {
    if (selected) {
        cairo_set_source_rgba(cr, palette->selection_r, palette->selection_g,
                              palette->selection_b, palette->selection_a);
        cairo_rectangle(cr, x, y, width, height);
        cairo_fill(cr);
        popup_draw_text(cr, font, text, x + 4, y + 2,
                        palette->selection_text_r, palette->selection_text_g,
                        palette->selection_text_b);
    } else {
        popup_draw_text(cr, font, text, x + 4, y + 2,
                        palette->text_r, palette->text_g, palette->text_b);
    }
}

static void popup_hide_surface(TypioWlPopup *popup) {
    if (!popup || !popup->surface || !popup->visible) {
        return;
    }

    wl_surface_attach(popup->surface, nullptr, 0, 0);
    wl_surface_commit(popup->surface);
    popup->visible = false;
}

static bool popup_ensure_created(TypioWlFrontend *frontend) {
    if (!frontend) {
        return false;
    }

    if (frontend->popup) {
        return frontend->popup->surface && frontend->popup->popup_surface;
    }

    if (!frontend->compositor || !frontend->shm || !frontend->input_method) {
        return false;
    }

    frontend->popup = typio_wl_popup_create(frontend);
    if (!frontend->popup) {
        typio_log(TYPIO_LOG_WARNING,
                  "Failed to create popup surface on demand; using inline candidates");
        return false;
    }

    typio_log(TYPIO_LOG_DEBUG, "Created popup candidate surface on demand");
    return true;
}

static bool popup_render(TypioWlPopup *popup, const TypioPreedit *preedit,
                         const TypioCandidateList *candidates) {
    cairo_surface_t *surface;
    cairo_t *cr;
    TypioWlPopupBuffer *buffer;
    TypioPopupRenderConfig render_config;
    const TypioPopupPalette *palette;
    TypioPopupLine *lines = nullptr;
    size_t line_count;
    char *preedit_text = nullptr;
    int preedit_width = 0;
    int preedit_height = 0;
    int content_width = 0;
    int content_height = 0;
    int items_height = 0;
    int row_width = 0;
    int row_height = 0;
    int width;
    int height;
    int scale;
    int buffer_width;
    int buffer_height;
    int y;

    if (!popup || !popup->surface || !candidates || candidates->count == 0) {
        return false;
    }

    if (preedit && preedit->segment_count > 0) {
        preedit_text = typio_wl_build_plain_preedit(preedit, nullptr);
    }

    popup_load_render_config(popup, &render_config);
    palette = popup_resolve_palette(render_config.theme_mode);

    line_count = candidates->count;
    lines = calloc(line_count, sizeof(*lines));
    if (!lines) {
        free(preedit_text);
        return false;
    }

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cr = cairo_create(surface);

    if (preedit_text && !popup_measure_text(cr, render_config.page_font_desc,
                                            preedit_text, &preedit_width,
                                            &preedit_height)) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        popup_free_lines(lines, line_count);
        free(preedit_text);
        return false;
    }

    content_width = preedit_width;
    content_height = preedit_text ? preedit_height : 0;
    for (size_t i = 0; i < line_count; ++i) {
        lines[i].text = popup_format_candidate(&candidates->candidates[i], i);
        lines[i].selected = (candidates->selected >= 0 &&
                             (size_t)candidates->selected == i);
        if (!lines[i].text ||
            !popup_measure_text(cr, render_config.font_desc, lines[i].text,
                                &lines[i].width, &lines[i].height)) {
            cairo_destroy(cr);
            cairo_surface_destroy(surface);
            popup_free_lines(lines, line_count);
            free(preedit_text);
            return false;
        }

        lines[i].width += 10;
        lines[i].height += 4;

        if (render_config.layout_mode == TYPIO_POPUP_LAYOUT_VERTICAL) {
            if (lines[i].width > content_width) {
                content_width = lines[i].width;
            }
            items_height += lines[i].height;
            if (i + 1 < line_count) {
                items_height += TYPIO_POPUP_ROW_GAP;
            }
            continue;
        }

        if (row_width > 0) {
            row_width += TYPIO_POPUP_COLUMN_GAP;
        }
        row_width += lines[i].width;
        if (lines[i].height > row_height) {
            row_height = lines[i].height;
        }
    }

    if (render_config.layout_mode == TYPIO_POPUP_LAYOUT_HORIZONTAL) {
        if (row_height > 0) {
            items_height += row_height;
        }
        if (row_width > content_width) {
            content_width = row_width;
        }
    }

    if (preedit_text && line_count > 0) {
        content_height += TYPIO_POPUP_SECTION_GAP;
    }
    content_height += items_height;

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    width = content_width + TYPIO_POPUP_PADDING * 2 + 8;
    if (width < TYPIO_POPUP_MIN_WIDTH) {
        width = TYPIO_POPUP_MIN_WIDTH;
    }
    height = content_height + TYPIO_POPUP_PADDING * 2;
    scale = popup_render_scale(popup);
    if (!popup_scaled_dimension(width, scale, &buffer_width) ||
        !popup_scaled_dimension(height, scale, &buffer_height)) {
        popup_free_lines(lines, line_count);
        free(preedit_text);
        return false;
    }

    buffer = popup_acquire_buffer(popup, buffer_width, buffer_height);
    if (!buffer) {
        popup_free_lines(lines, line_count);
        free(preedit_text);
        return false;
    }

    memset(buffer->data, 0, buffer->size);
    surface = cairo_image_surface_create_for_data((unsigned char *)buffer->data,
                                                  CAIRO_FORMAT_ARGB32,
                                                  buffer_width, buffer_height,
                                                  buffer->stride);
    cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);

    cairo_set_source_rgba(cr, palette->bg_r, palette->bg_g, palette->bg_b, palette->bg_a);
    cairo_paint(cr);

    cairo_set_source_rgba(cr, palette->border_r, palette->border_g,
                          palette->border_b, palette->border_a);
    cairo_rectangle(cr, 0.5, 0.5, width - 1.0, height - 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    y = TYPIO_POPUP_PADDING;
    if (preedit_text) {
        popup_draw_text(cr, render_config.page_font_desc, preedit_text,
                        TYPIO_POPUP_PADDING, y,
                        palette->preedit_r, palette->preedit_g, palette->preedit_b);
        y += preedit_height + TYPIO_POPUP_SECTION_GAP;
    }

    if (render_config.layout_mode == TYPIO_POPUP_LAYOUT_VERTICAL) {
        for (size_t i = 0; i < line_count; ++i) {
            popup_draw_candidate_row(cr, TYPIO_POPUP_PADDING - 2, y - 2,
                                     width - (TYPIO_POPUP_PADDING * 2) + 4,
                                     lines[i].height, lines[i].text, lines[i].selected,
                                     render_config.font_desc, palette);
            y += lines[i].height;
            if (i + 1 < line_count) {
                y += TYPIO_POPUP_ROW_GAP;
            }
        }
    } else {
        int x = TYPIO_POPUP_PADDING;
        int current_row_height = 0;

        for (size_t i = 0; i < line_count; ++i) {
            lines[i].x = x;
            lines[i].y = y;
            popup_draw_candidate_row(cr, x, y, lines[i].width, lines[i].height,
                                     lines[i].text, lines[i].selected,
                                     render_config.font_desc, palette);
            x += lines[i].width + TYPIO_POPUP_COLUMN_GAP;
            if (lines[i].height > current_row_height) {
                current_row_height = lines[i].height;
            }
        }

        if (current_row_height > 0) {
            y += current_row_height;
        }
    }

    cairo_surface_flush(surface);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    wl_surface_set_buffer_scale(popup->surface, scale);
    wl_surface_attach(popup->surface, buffer->buffer, 0, 0);
    wl_surface_damage(popup->surface, 0, 0, width, height);
    wl_surface_commit(popup->surface);
    buffer->busy = true;
    popup->visible = true;

    popup_free_lines(lines, line_count);
    free(preedit_text);
    return true;
}

static void popup_handle_text_input_rectangle(void *data,
                                              [[maybe_unused]] struct zwp_input_popup_surface_v2 *popup_surface,
                                              int32_t x, int32_t y,
                                              int32_t width, int32_t height) {
    TypioWlPopup *popup = data;

    popup->text_input_x = x;
    popup->text_input_y = y;
    popup->text_input_width = width;
    popup->text_input_height = height;
}

static void popup_handle_surface_enter(void *data,
                                       [[maybe_unused]] struct wl_surface *surface,
                                       struct wl_output *output) {
    popup_track_output(data, output);
}

static void popup_handle_surface_leave(void *data,
                                       [[maybe_unused]] struct wl_surface *surface,
                                       struct wl_output *output) {
    popup_untrack_output(data, output);
}

TypioWlPopup *typio_wl_popup_create(TypioWlFrontend *frontend) {
    TypioWlPopup *popup;

    if (!frontend || !frontend->compositor || !frontend->shm || !frontend->input_method) {
        return nullptr;
    }

    popup = calloc(1, sizeof(*popup));
    if (!popup) {
        return nullptr;
    }

    popup->frontend = frontend;
    popup->surface = wl_compositor_create_surface(frontend->compositor);
    if (!popup->surface) {
        free(popup);
        return nullptr;
    }
    wl_surface_add_listener(popup->surface, &popup_surface_listener, popup);

    popup->popup_surface = zwp_input_method_v2_get_input_popup_surface(
        frontend->input_method, popup->surface);
    if (!popup->popup_surface) {
        wl_surface_destroy(popup->surface);
        free(popup);
        return nullptr;
    }

    zwp_input_popup_surface_v2_add_listener(popup->popup_surface,
                                            &popup_listener, popup);
    typio_log(TYPIO_LOG_DEBUG, "Input popup surface initialized");
    return popup;
}

void typio_wl_popup_destroy(TypioWlPopup *popup) {
    if (!popup) {
        return;
    }

    popup_hide_surface(popup);
    for (size_t i = 0; i < TYPIO_POPUP_BUFFER_COUNT; ++i) {
        popup_buffer_reset(&popup->buffers[i]);
    }
    popup_clear_outputs(popup);

    if (popup->popup_surface) {
        zwp_input_popup_surface_v2_destroy(popup->popup_surface);
    }
    if (popup->surface) {
        wl_surface_destroy(popup->surface);
    }

    free(popup);
}

bool typio_wl_popup_update(TypioWlFrontend *frontend, TypioInputContext *ctx) {
    const TypioCandidateList *candidates;

    if (!frontend || !ctx) {
        return false;
    }

    if (!popup_ensure_created(frontend)) {
        return false;
    }

    candidates = typio_input_context_get_candidates(ctx);

    if (!candidates || candidates->count == 0) {
        popup_hide_surface(frontend->popup);
        return true;
    }

    /* Preedit is already shown inline via set_preedit_string;
     * pass nullptr to avoid duplicating it inside the popup. */
    if (!popup_render(frontend->popup, nullptr, candidates)) {
        typio_log(TYPIO_LOG_WARNING,
                  "Popup render failed; falling back to inline candidate UI");
        popup_hide_surface(frontend->popup);
        return false;
    }

    typio_log(TYPIO_LOG_DEBUG, "Rendered popup candidates (%zu items)",
              candidates->count);
    return true;
}

void typio_wl_popup_hide(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->popup) {
        return;
    }

    popup_hide_surface(frontend->popup);
}

bool typio_wl_popup_is_available(TypioWlFrontend *frontend) {
    return frontend && frontend->popup && frontend->popup->surface &&
           frontend->popup->popup_surface;
}

void typio_wl_popup_handle_output_change(TypioWlFrontend *frontend,
                                         struct wl_output *output) {
    TypioWlPopup *popup;

    if (!frontend || !output) {
        return;
    }

    popup = frontend->popup;
    if (!popup || !popup_tracks_output(popup, output)) {
        return;
    }

    if (!popup_find_frontend_output(popup, output)) {
        popup_untrack_output(popup, output);
        return;
    }

    popup_refresh_visible_surface(popup);
}
