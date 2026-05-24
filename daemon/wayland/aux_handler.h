/**
 * @file aux_handler.h
 * @brief Optional-feature abstraction layer for the Wayland event loop.
 *
 * Instead of #ifdef’ing struct members and loop branches, every optional
 * subsystem (status bus, tray, voice) registers a TypioWlAuxHandler.
 * The event loop treats them uniformly: ask for an fd, poll it, dispatch it.
 *
 * This keeps TypioWlFrontend layout stable across build configurations and
 * makes the event loop code readable.
 */

#ifndef TYPIO_WL_AUX_HANDLER_H
#define TYPIO_WL_AUX_HANDLER_H

#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioWlAuxHandler TypioWlAuxHandler;
typedef struct TypioWlFrontend TypioWlFrontend;

struct TypioWlAuxHandler {
    const char *name;                           /**< Human-readable tag for logs */
    int (*get_fd)(TypioWlAuxHandler *self);     /**< Return pollable fd or -1 */
    void (*on_ready)(TypioWlAuxHandler *self);  /**< Called when fd is readable */
    void (*destroy)(TypioWlAuxHandler *self);   /**< Tear down the handler */
};

/**
 * @brief Convenience constructor wrapping an external subsystem.
 *
 * @param name     Tag for logging.
 * @param fd_fn    Returns the pollable fd (may be -1 when disabled).
 * @param ready_fn Called when the fd is readable.
 * @param free_fn  Optional destructor (may be NULL).
 * @param userdata Opaque pointer passed to all callbacks.
 */
TypioWlAuxHandler *typio_wl_aux_handler_new(const char *name,
                                             int (*fd_fn)(void *),
                                             void (*ready_fn)(void *),
                                             void (*free_fn)(void *),
                                             void *userdata);
void typio_wl_aux_handler_free(TypioWlAuxHandler *handler);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_AUX_HANDLER_H */
