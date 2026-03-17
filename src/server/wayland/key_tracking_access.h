/**
 * @file key_tracking_access.h
 * @brief Inline accessors for per-key tracking state stored on TypioWlFrontend
 */

#ifndef TYPIO_WL_KEY_TRACKING_ACCESS_H
#define TYPIO_WL_KEY_TRACKING_ACCESS_H

#include "wl_frontend_internal.h"

static inline TypioKeyTrackState key_get_state(TypioWlFrontend *fe, uint32_t key) {
    return (key < TYPIO_WL_MAX_TRACKED_KEYS) ? fe->key_states[key] : TYPIO_KEY_IDLE;
}

static inline void key_set_state(TypioWlFrontend *fe, uint32_t key,
                                 TypioKeyTrackState st) {
    if (key < TYPIO_WL_MAX_TRACKED_KEYS) {
        fe->key_states[key] = st;
    }
}

static inline uint32_t key_get_generation(TypioWlFrontend *fe, uint32_t key) {
    return (key < TYPIO_WL_MAX_TRACKED_KEYS) ? fe->key_generations[key] : 0;
}

static inline void key_set_generation(TypioWlFrontend *fe, uint32_t key,
                                      uint32_t generation) {
    if (key < TYPIO_WL_MAX_TRACKED_KEYS) {
        fe->key_generations[key] = generation;
    }
}

static inline void key_claim_current_generation(TypioWlFrontend *fe, uint32_t key) {
    key_set_generation(fe, key, fe->active_key_generation);
    fe->active_generation_owned_keys = true;
}

static inline void key_clear_tracking(TypioWlFrontend *fe, uint32_t key) {
    key_set_state(fe, key, TYPIO_KEY_IDLE);
    key_set_generation(fe, key, 0);
}

static inline bool key_owned_by_active_generation(TypioWlFrontend *fe, uint32_t key) {
    return fe && fe->active_key_generation != 0 &&
           key_get_generation(fe, key) == fe->active_key_generation;
}

#endif /* TYPIO_WL_KEY_TRACKING_ACCESS_H */
