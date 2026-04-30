/**
 * @file compose.h
 * @brief Compose/dead-key state machine for the basic keyboard engine
 *
 * Implements two-key compose sequences for accented and special characters
 * (e.g. ' + a -> á, ` + e -> è, ~ + n -> ñ).
 */

#ifndef BASIC_COMPOSE_H
#define BASIC_COMPOSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BasicCompose BasicCompose;

typedef enum {
    BASIC_COMPOSE_NONE = 0,     /**< Not a compose sequence, pass through */
    BASIC_COMPOSE_CONSUME,      /**< First key consumed, waiting for second */
    BASIC_COMPOSE_COMMIT,       /**< Sequence complete, result in out_codepoints */
    BASIC_COMPOSE_CANCEL,       /**< Sequence cancelled, flush buffered key(s), re-process current key */
} BasicComposeResult;

/**
 * @brief Create a new compose state machine.
 */
BasicCompose *basic_compose_new(void);

/**
 * @brief Destroy a compose state machine.
 */
void basic_compose_free(BasicCompose *state);

/**
 * @brief Process a printable key codepoint.
 *
 * @param state          Compose state.
 * @param codepoint      Unicode codepoint of the pressed key.
 * @param out_codepoints Output buffer for resulting codepoints (size 4).
 * @param out_count      Set to number of codepoints written to out_codepoints.
 * @return Result indicating how the caller should proceed.
 *
 * @note If result is BASIC_COMPOSE_CANCEL, the caller should commit out_codepoints
 *       and then treat the current @p codepoint as an unhandled key.
 */
BasicComposeResult basic_compose_process_key(BasicCompose *state,
                                              uint32_t codepoint,
                                              uint32_t out_codepoints[4],
                                              size_t *out_count);

/**
 * @brief Get the current preedit text (the buffered first key).
 * @return UTF-8 string or NULL if idle.
 */
const char *basic_compose_get_preedit(const BasicCompose *state);

/**
 * @brief Cancel the current composition and return the buffered first key.
 * @return The buffered first codepoint, or 0 if idle.
 */
uint32_t basic_compose_cancel(BasicCompose *state);

/**
 * @brief Reset the compose state to idle.
 */
void basic_compose_reset(BasicCompose *state);

/**
 * @brief Check if the compose state machine is actively waiting for a second key.
 */
bool basic_compose_is_active(const BasicCompose *state);

#ifdef __cplusplus
}
#endif

#endif /* BASIC_COMPOSE_H */
