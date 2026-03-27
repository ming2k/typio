#ifndef TYPIO_WL_CANDIDATE_POPUP_DAMAGE_H
#define TYPIO_WL_CANDIDATE_POPUP_DAMAGE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct TypioCandidatePopupDamageLine {
    int x;
    int y;
    int width;
    int height;
} TypioCandidatePopupDamageLine;

typedef struct TypioCandidatePopupDamageRect {
    int x;
    int y;
    int width;
    int height;
    bool valid;
} TypioCandidatePopupDamageRect;

bool typio_candidate_popup_damage_union(const TypioCandidatePopupDamageLine *lines,
                              size_t line_count,
                              TypioCandidatePopupDamageRect *rect);

#endif /* TYPIO_WL_CANDIDATE_POPUP_DAMAGE_H */
