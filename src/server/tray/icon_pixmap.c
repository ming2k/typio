/**
 * @file icon_pixmap.c
 * @brief Pixmap builder for SNI IconPixmap fallback — Cairo-drawn icons
 *
 * The primary icon path is IconName + IconThemePath (SVG symbolic icons).
 * This file provides the IconPixmap D-Bus property as a fallback for
 * panels that don't support icon theme lookup.
 */

#include "icon_pixmap.h"

#include <cairo.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool icon_is_rime(const char *icon_name) {
    return icon_name && strstr(icon_name, "rime") != NULL;
}

/**
 * Convert a Cairo ARGB32 surface to the ARGB network byte-order format
 * required by the SNI IconPixmap D-Bus property.
 */
static bool surface_to_argb(cairo_surface_t *surface, int size,
                             unsigned char **data_out, int *data_len_out) {
    uint32_t *pixels;
    unsigned char *argb;
    int stride;

    cairo_surface_flush(surface);
    stride = cairo_image_surface_get_stride(surface);
    pixels = (uint32_t *)cairo_image_surface_get_data(surface);

    argb = calloc((size_t)size * (size_t)size * 4U, sizeof(unsigned char));
    if (!argb) {
        return false;
    }

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint32_t pixel = *(uint32_t *)((unsigned char *)pixels + y * stride + x * 4);
            size_t offset = ((size_t)y * (size_t)size + (size_t)x) * 4U;
            argb[offset + 0] = (unsigned char)((pixel >> 24) & 0xffU);
            argb[offset + 1] = (unsigned char)((pixel >> 16) & 0xffU);
            argb[offset + 2] = (unsigned char)((pixel >> 8) & 0xffU);
            argb[offset + 3] = (unsigned char)(pixel & 0xffU);
        }
    }

    *data_out = argb;
    *data_len_out = size * size * 4;
    return true;
}

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

bool typio_tray_icon_pixmap_build(const char *icon_name, int preferred_size,
                                  int *width_out, int *height_out,
                                  unsigned char **data_out, int *data_len_out) {
    cairo_surface_t *surface;
    cairo_t *cr;
    bool ok;
    int size;

    if (!width_out || !height_out || !data_out || !data_len_out) {
        return false;
    }

    *width_out = 0;
    *height_out = 0;
    *data_out = NULL;
    *data_len_out = 0;

    size = preferred_size > 0 ? preferred_size : 64;

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

    if (ok) {
        *width_out = size;
        *height_out = size;
    }
    return ok;
}

void typio_tray_icon_pixmap_free(unsigned char *data) {
    free(data);
}
