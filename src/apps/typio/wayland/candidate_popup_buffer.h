/**
 * @file candidate_popup_buffer.h
 * @brief Wayland SHM buffer pool for candidate popup rendering
 */

#ifndef TYPIO_WL_CANDIDATE_POPUP_BUFFER_H
#define TYPIO_WL_CANDIDATE_POPUP_BUFFER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wl_buffer;
struct wl_shm;

typedef struct TypioCandidatePopupBuffer {
    struct wl_buffer *buffer;
    void *data;
    size_t size;
    int width;
    int height;
    int stride;
    bool busy;
} TypioCandidatePopupBuffer;

#define TYPIO_CANDIDATE_POPUP_BUFFER_COUNT 3

void typio_candidate_popup_buffer_reset(TypioCandidatePopupBuffer *buffer);
bool typio_candidate_popup_buffer_create(TypioCandidatePopupBuffer *buffer,
                               struct wl_shm *shm,
                               int width, int height);
TypioCandidatePopupBuffer *typio_candidate_popup_buffer_acquire(TypioCandidatePopupBuffer *pool,
                                             size_t pool_size,
                                             struct wl_shm *shm,
                                             int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_CANDIDATE_POPUP_BUFFER_H */
