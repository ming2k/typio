/**
 * @file result.h
 * @brief Standardised error-handling macros and helpers for Typio core
 *
 * All TypioResult-returning functions should use TYPIO_RETURN_IF_ERROR() to
 * propagate failures.  This keeps the happy path left-aligned and makes error
 * paths explicit.
 */

#ifndef TYPIO_RESULT_H
#define TYPIO_RESULT_H

#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Propagate a TypioResult if it is not TYPIO_OK.
 *
 * Usage:
 *   TYPIO_RETURN_IF_ERROR(typio_xyz_create(...));
 */
#define TYPIO_RETURN_IF_ERROR(expr)                        \
    do {                                                   \
        TypioResult _r = (expr);                           \
        if (_r != TYPIO_OK) return _r;                     \
    } while (0)

/**
 * @brief Jump to a cleanup label if a TypioResult is not TYPIO_OK.
 *
 * The variable @c _result must be in scope and will be assigned the
 * expression's return value.
 *
 * Usage:
 *   TypioResult _result = TYPIO_OK;
 *   TYPIO_GOTO_IF_ERROR(typio_xyz_create(...), cleanup);
 */
#define TYPIO_GOTO_IF_ERROR(expr, label)                   \
    do {                                                   \
        _result = (expr);                                  \
        if (_result != TYPIO_OK) goto label;               \
    } while (0)

/**
 * @brief Jump to a cleanup label if a pointer is NULL.
 *
 * Sets _result to TYPIO_ERROR_OUT_OF_MEMORY or TYPIO_ERROR_INVALID_ARGUMENT
 * depending on whether the pointer is an output argument.
 */
#define TYPIO_GOTO_IF_NULL(ptr, label)                     \
    do {                                                   \
        if ((ptr) == nullptr) {                            \
            _result = TYPIO_ERROR_INVALID_ARGUMENT;        \
            goto label;                                    \
        }                                                  \
    } while (0)

/**
 * @brief Convert a TypioResult to a human-readable C string.
 */
const char *typio_result_to_string(TypioResult result);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_RESULT_H */
