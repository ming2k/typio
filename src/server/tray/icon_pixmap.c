/**
 * @file icon_pixmap.c
 * @brief Pixmap builder for tray icons — loads bundled PNGs, falls back to Cairo
 */

#include "icon_pixmap.h"
#include "typio_build_config.h"

#include <cairo.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool icon_is_rime(const char *icon_name) {
    return icon_name && strstr(icon_name, "rime") != NULL;
}

/* Try standard hicolor sizes in descending preference for the requested size */
static const int icon_sizes[] = {48, 64, 32, 128, 24, 22, 16, 192};
#define ICON_SIZES_COUNT (sizeof(icon_sizes) / sizeof(icon_sizes[0]))

/**
 * Find the best bundled PNG for the given icon name and preferred size.
 * Searches both source and install icon directories.
 */
static cairo_surface_t *load_bundled_icon(const char *icon_name, int preferred_size) {
    const char *dirs[] = { TYPIO_SOURCE_ICON_DIR, TYPIO_INSTALL_ICON_DIR };
    char path[512];
    cairo_surface_t *surface;

    if (!icon_name || !icon_name[0]) {
        return NULL;
    }

    /* First try exact preferred size, then other sizes */
    for (size_t d = 0; d < 2; ++d) {
        if (access(dirs[d], R_OK) != 0) {
            continue;
        }

        /* Try exact size first */
        snprintf(path, sizeof(path), "%s/hicolor/%dx%d/apps/%s.png",
                 dirs[d], preferred_size, preferred_size, icon_name);
        surface = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
            return surface;
        }
        cairo_surface_destroy(surface);

        /* Try other standard sizes */
        for (size_t i = 0; i < ICON_SIZES_COUNT; ++i) {
            if (icon_sizes[i] == preferred_size) {
                continue;
            }
            snprintf(path, sizeof(path), "%s/hicolor/%dx%d/apps/%s.png",
                     dirs[d], icon_sizes[i], icon_sizes[i], icon_name);
            surface = cairo_image_surface_create_from_png(path);
            if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
                return surface;
            }
            cairo_surface_destroy(surface);
        }
    }

    return NULL;
}

/**
 * Convert a Cairo ARGB32 surface to the ARGB network byte-order format
 * required by the SNI IconPixmap D-Bus property, scaling to target_size.
 */
static bool surface_to_argb(cairo_surface_t *source, int target_size,
                             unsigned char **data_out, int *data_len_out) {
    cairo_surface_t *scaled;
    cairo_t *cr;
    int src_w, src_h;
    uint32_t *pixels;
    unsigned char *argb;
    int stride;
    double sx, sy;

    src_w = cairo_image_surface_get_width(source);
    src_h = cairo_image_surface_get_height(source);

    /* Scale if needed */
    if (src_w == target_size && src_h == target_size) {
        cairo_surface_reference(source);
        scaled = source;
    } else {
        scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                            target_size, target_size);
        cr = cairo_create(scaled);
        sx = (double)target_size / src_w;
        sy = (double)target_size / src_h;
        cairo_scale(cr, sx, sy);
        cairo_set_source_surface(cr, source, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
    }

    cairo_surface_flush(scaled);
    stride = cairo_image_surface_get_stride(scaled);
    pixels = (uint32_t *)cairo_image_surface_get_data(scaled);

    argb = calloc((size_t)target_size * (size_t)target_size * 4U, sizeof(unsigned char));
    if (!argb) {
        cairo_surface_destroy(scaled);
        return false;
    }

    for (int y = 0; y < target_size; ++y) {
        for (int x = 0; x < target_size; ++x) {
            uint32_t pixel = *(uint32_t *)((unsigned char *)pixels + y * stride + x * 4);
            size_t offset = ((size_t)y * (size_t)target_size + (size_t)x) * 4U;
            argb[offset + 0] = (unsigned char)((pixel >> 24) & 0xffU);
            argb[offset + 1] = (unsigned char)((pixel >> 16) & 0xffU);
            argb[offset + 2] = (unsigned char)((pixel >> 8) & 0xffU);
            argb[offset + 3] = (unsigned char)(pixel & 0xffU);
        }
    }

    cairo_surface_destroy(scaled);
    *data_out = argb;
    *data_len_out = target_size * target_size * 4;
    return true;
}

/* Cairo fallback drawing */

static void draw_keyboard_icon(cairo_t *cr, int size) {
    const double pad = size * 0.14;
    const double body_w = size - pad * 2.0;
    const double body_h = size * 0.42;
    const double body_y = size * 0.29;
    const double radius = size * 0.08;

    cairo_set_source_rgba(cr, 0.18, 0.23, 0.29, 1.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, pad + body_w - radius, body_y + radius, radius, -1.5708, 0.0);
    cairo_arc(cr, pad + body_w - radius, body_y + body_h - radius, radius, 0.0, 1.5708);
    cairo_arc(cr, pad + radius, body_y + body_h - radius, radius, 1.5708, 3.14159);
    cairo_arc(cr, pad + radius, body_y + radius, radius, 3.14159, 4.71239);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.92);
    const int cols = 5;
    const int rows = 2;
    const double key_w = body_w * 0.12;
    const double key_h = body_h * 0.16;
    const double gap_x = body_w * 0.06;
    const double gap_y = body_h * 0.16;

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            double x = pad + body_w * 0.10 + col * (key_w + gap_x);
            double y = body_y + body_h * 0.18 + row * (key_h + gap_y);
            cairo_rectangle(cr, x, y, key_w, key_h);
            cairo_fill(cr);
        }
    }

    cairo_rectangle(cr, pad + body_w * 0.26, body_y + body_h * 0.68,
                    body_w * 0.48, key_h);
    cairo_fill(cr);
}

static void draw_rime_icon(cairo_t *cr, int size) {
    const double pad = size * 0.10;
    const double radius = size * 0.20;

    cairo_set_source_rgba(cr, 0.78, 0.17, 0.12, 1.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, size - pad - radius, pad + radius, radius, -1.5708, 0.0);
    cairo_arc(cr, size - pad - radius, size - pad - radius, radius, 0.0, 1.5708);
    cairo_arc(cr, pad + radius, size - pad - radius, radius, 1.5708, 3.14159);
    cairo_arc(cr, pad + radius, pad + radius, radius, 3.14159, 4.71239);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, size * 0.52);

    cairo_text_extents_t extents;
    cairo_text_extents(cr, "R", &extents);
    cairo_move_to(cr,
                  (size - extents.width) / 2.0 - extents.x_bearing,
                  (size - extents.height) / 2.0 - extents.y_bearing);
    cairo_show_text(cr, "R");
}

static bool draw_fallback_icon(const char *icon_name, int size,
                                unsigned char **data_out, int *data_len_out) {
    cairo_surface_t *surface;
    cairo_t *cr;
    bool ok;

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return false;
    }

    cr = cairo_create(surface);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    if (icon_is_rime(icon_name)) {
        draw_rime_icon(cr, size);
    } else {
        draw_keyboard_icon(cr, size);
    }

    cairo_destroy(cr);
    ok = surface_to_argb(surface, size, data_out, data_len_out);
    cairo_surface_destroy(surface);
    return ok;
}

bool typio_tray_icon_pixmap_build(const char *icon_name, int preferred_size,
                                  int *width_out, int *height_out,
                                  unsigned char **data_out, int *data_len_out) {
    cairo_surface_t *png;
    int size;

    if (!width_out || !height_out || !data_out || !data_len_out) {
        return false;
    }

    *width_out = 0;
    *height_out = 0;
    *data_out = NULL;
    *data_len_out = 0;

    size = preferred_size > 0 ? preferred_size : 64;

    /* Try loading bundled PNG first */
    png = load_bundled_icon(icon_name, size);
    if (png) {
        bool ok = surface_to_argb(png, size, data_out, data_len_out);
        cairo_surface_destroy(png);
        if (ok) {
            *width_out = size;
            *height_out = size;
            return true;
        }
    }

    /* Fall back to Cairo-drawn icons */
    if (!draw_fallback_icon(icon_name, size, data_out, data_len_out)) {
        return false;
    }

    *width_out = size;
    *height_out = size;
    return true;
}

void typio_tray_icon_pixmap_free(unsigned char *data) {
    free(data);
}
