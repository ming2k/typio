#include "popup_damage.h"

static void popup_damage_include_line(TypioPopupDamageRect *rect,
                                      const TypioPopupDamageLine *line) {
    int right;
    int bottom;

    if (!rect || !line || line->width <= 0 || line->height <= 0) {
        return;
    }

    right = line->x + line->width;
    bottom = line->y + line->height;

    if (!rect->valid) {
        rect->x = line->x;
        rect->y = line->y;
        rect->width = line->width;
        rect->height = line->height;
        rect->valid = true;
        return;
    }

    if (line->x < rect->x) {
        rect->width += rect->x - line->x;
        rect->x = line->x;
    }
    if (line->y < rect->y) {
        rect->height += rect->y - line->y;
        rect->y = line->y;
    }
    if (right > rect->x + rect->width) {
        rect->width = right - rect->x;
    }
    if (bottom > rect->y + rect->height) {
        rect->height = bottom - rect->y;
    }
}

bool typio_popup_damage_union(const TypioPopupDamageLine *lines,
                              size_t line_count,
                              TypioPopupDamageRect *rect) {
    if (!rect) {
        return false;
    }

    rect->x = 0;
    rect->y = 0;
    rect->width = 0;
    rect->height = 0;
    rect->valid = false;

    if (!lines || line_count == 0) {
        return false;
    }

    for (size_t i = 0; i < line_count; ++i) {
        popup_damage_include_line(rect, &lines[i]);
    }

    return rect->valid;
}
