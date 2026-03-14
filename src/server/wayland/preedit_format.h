/**
 * @file preedit_format.h
 * @brief Formatting helpers for plain preedit display
 */

#ifndef TYPIO_WL_PREEDIT_FORMAT_H
#define TYPIO_WL_PREEDIT_FORMAT_H

#include "typio/input_context.h"

#ifdef __cplusplus
extern "C" {
#endif

char *typio_wl_build_plain_preedit(const TypioPreedit *preedit, int *cursor_pos);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_PREEDIT_FORMAT_H */
