/**
 * @file frontend_aux.h
 * @brief Policy helpers for non-Wayland auxiliary event sources
 */

#ifndef TYPIO_WL_FRONTEND_AUX_H
#define TYPIO_WL_FRONTEND_AUX_H

#include <poll.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioWlAuxState {
    int fd;
    bool disabled;
} TypioWlAuxState;

bool typio_wl_aux_should_disable_on_revents(short revents);
bool typio_wl_aux_should_disable_on_dispatch_result(int dispatch_result);
TypioWlAuxState typio_wl_aux_apply_transition(TypioWlAuxState state,
                                              short revents,
                                              int dispatch_result,
                                              int next_fd);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_FRONTEND_AUX_H */
