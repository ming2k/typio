#ifndef TYPIO_WL_POPUP_DAMAGE_H
#define TYPIO_WL_POPUP_DAMAGE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct TypioPopupDamageLine {
    int x;
    int y;
    int width;
    int height;
} TypioPopupDamageLine;

typedef struct TypioPopupDamageRect {
    int x;
    int y;
    int width;
    int height;
    bool valid;
} TypioPopupDamageRect;

bool typio_popup_damage_union(const TypioPopupDamageLine *lines,
                              size_t line_count,
                              TypioPopupDamageRect *rect);

#endif /* TYPIO_WL_POPUP_DAMAGE_H */
