/**
 * @file aux_handler.c
 * @brief TypioWlAuxHandler default implementation
 */

#include "aux_handler.h"
#include "utils/log.h"
#include <stdlib.h>

typedef struct {
    TypioWlAuxHandler base;
    int (*fd_fn)(void *);
    void (*ready_fn)(void *);
    void (*free_fn)(void *);
    void *userdata;
} TypioWlAuxHandlerImpl;

static int default_get_fd(TypioWlAuxHandler *self) {
    TypioWlAuxHandlerImpl *impl = (TypioWlAuxHandlerImpl *)self;
    if (!impl->fd_fn) return -1;
    return impl->fd_fn(impl->userdata);
}

static void default_on_ready(TypioWlAuxHandler *self) {
    TypioWlAuxHandlerImpl *impl = (TypioWlAuxHandlerImpl *)self;
    if (impl->ready_fn) {
        impl->ready_fn(impl->userdata);
    }
}

static void default_destroy(TypioWlAuxHandler *self) {
    TypioWlAuxHandlerImpl *impl = (TypioWlAuxHandlerImpl *)self;
    if (impl->free_fn) {
        impl->free_fn(impl->userdata);
    }
    free(impl);
}

TypioWlAuxHandler *typio_wl_aux_handler_new(const char *name,
                                             int (*fd_fn)(void *),
                                             void (*ready_fn)(void *),
                                             void (*free_fn)(void *),
                                             void *userdata) {
    TypioWlAuxHandlerImpl *impl = calloc(1, sizeof(*impl));
    if (!impl) return nullptr;

    impl->base.name    = name ? name : "anonymous";
    impl->base.get_fd  = default_get_fd;
    impl->base.on_ready = default_on_ready;
    impl->base.destroy = default_destroy;
    impl->fd_fn       = fd_fn;
    impl->ready_fn    = ready_fn;
    impl->free_fn     = free_fn;
    impl->userdata    = userdata;

    return &impl->base;
}

void typio_wl_aux_handler_free(TypioWlAuxHandler *handler) {
    if (handler && handler->destroy) {
        handler->destroy(handler);
    }
}
