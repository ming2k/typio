/**
 * @file inline_ui.h
 * @brief Formatting helpers for inline preedit and candidate display
 */

#ifndef TYPIO_WL_INLINE_UI_H
#define TYPIO_WL_INLINE_UI_H

#include "typio/input_context.h"

#ifdef __cplusplus
extern "C" {
#endif

char *typio_wl_build_inline_preedit(const TypioPreedit *preedit,
                                    const TypioCandidateList *candidates,
                                    int *cursor_pos);
char *typio_wl_build_plain_preedit(const TypioPreedit *preedit, int *cursor_pos);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_INLINE_UI_H */
