/**
 * @file frontend_aux.c
 * @brief Policy helpers for non-Wayland auxiliary event sources
 */

#include "frontend_aux.h"

bool typio_wl_aux_should_disable_on_revents(short revents) {
    return (revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
}

bool typio_wl_aux_should_disable_on_dispatch_result(int dispatch_result) {
    return dispatch_result < 0;
}

TypioWlAuxState typio_wl_aux_apply_transition(TypioWlAuxState state,
                                              short revents,
                                              int dispatch_result,
                                              int next_fd) {
    if (typio_wl_aux_should_disable_on_revents(revents) ||
        typio_wl_aux_should_disable_on_dispatch_result(dispatch_result)) {
        state.fd = -1;
        state.disabled = true;
        return state;
    }

    if ((revents & POLLIN) != 0 && dispatch_result >= 0) {
        state.fd = next_fd;
    }

    return state;
}
