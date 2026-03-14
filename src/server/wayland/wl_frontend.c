/**
 * @file wl_frontend.c
 * @brief Wayland input method frontend implementation
 */

#include "wl_frontend.h"
#include "wl_frontend_internal.h"
#include "typio/typio.h"
#include "utils/log.h"

#include <wayland-client.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* Registry listener */
static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version);
static void registry_handle_global_remove(void *data, struct wl_registry *registry,
                                          uint32_t name);

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

/* Seat listener */
static void seat_handle_capabilities(void *data, struct wl_seat *seat,
                                     uint32_t capabilities);
static void seat_handle_name(void *data, struct wl_seat *seat, const char *name);

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

TypioWlFrontend *typio_wl_frontend_new(TypioInstance *instance,
                                        const TypioWlFrontendConfig *config) {
    if (!instance) {
        return NULL;
    }

    TypioWlFrontend *frontend = calloc(1, sizeof(TypioWlFrontend));
    if (!frontend) {
        return NULL;
    }

    frontend->instance = instance;

    /* Connect to Wayland display */
    const char *display_name = config ? config->display_name : NULL;
    frontend->display = wl_display_connect(display_name);
    if (!frontend->display) {
        typio_log(TYPIO_LOG_ERROR, "Failed to connect to Wayland display");
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "Failed to connect to Wayland display: %s",
                 display_name ? display_name : "(default)");
        free(frontend);
        return NULL;
    }

    typio_log(TYPIO_LOG_INFO, "Connected to Wayland display");

    /* Get registry */
    frontend->registry = wl_display_get_registry(frontend->display);
    if (!frontend->registry) {
        typio_log(TYPIO_LOG_ERROR, "Failed to get Wayland registry");
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "Failed to get Wayland registry");
        wl_display_disconnect(frontend->display);
        free(frontend);
        return NULL;
    }

    wl_registry_add_listener(frontend->registry, &registry_listener, frontend);

    /* Roundtrip to get globals */
    if (wl_display_roundtrip(frontend->display) < 0) {
        typio_log(TYPIO_LOG_ERROR, "Wayland roundtrip failed");
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "Wayland roundtrip failed");
        wl_registry_destroy(frontend->registry);
        wl_display_disconnect(frontend->display);
        free(frontend);
        return NULL;
    }

    /* Verify we have required interfaces */
    if (!frontend->im_manager) {
        typio_log(TYPIO_LOG_ERROR,
                  "Compositor does not support zwp_input_method_manager_v2");
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "Session does not provide the required Wayland input-method/text-input protocol stack");
        if (frontend->seat) {
            wl_seat_destroy(frontend->seat);
        }
        wl_registry_destroy(frontend->registry);
        wl_display_disconnect(frontend->display);
        free(frontend);
        return NULL;
    }

    if (!frontend->seat) {
        typio_log(TYPIO_LOG_ERROR, "No seat available");
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "No seat available");
        zwp_input_method_manager_v2_destroy(frontend->im_manager);
        wl_registry_destroy(frontend->registry);
        wl_display_disconnect(frontend->display);
        free(frontend);
        return NULL;
    }

    if (!frontend->compositor || !frontend->shm) {
        typio_log(TYPIO_LOG_WARNING,
                  "Compositor missing wl_compositor or wl_shm; popup candidates disabled");
    }

    /* Create input method */
    frontend->input_method = zwp_input_method_manager_v2_get_input_method(
        frontend->im_manager, frontend->seat);
    if (!frontend->input_method) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create input method");
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "Failed to create input method");
        wl_seat_destroy(frontend->seat);
        zwp_input_method_manager_v2_destroy(frontend->im_manager);
        wl_registry_destroy(frontend->registry);
        wl_display_disconnect(frontend->display);
        free(frontend);
        return NULL;
    }

    /* Set up input method listener */
    typio_wl_input_method_setup(frontend);

    /* Create virtual keyboard for forwarding unhandled keys */
    if (frontend->vk_manager && frontend->seat) {
        frontend->virtual_keyboard =
            zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
                frontend->vk_manager, frontend->seat);
        if (frontend->virtual_keyboard) {
            typio_log(TYPIO_LOG_INFO, "Virtual keyboard created for key forwarding");
        } else {
            typio_log(TYPIO_LOG_WARNING,
                      "Failed to create virtual keyboard; unhandled keys will be lost");
        }
    } else {
        typio_log(TYPIO_LOG_WARNING,
                  "No virtual keyboard manager; unhandled keys will be lost");
    }

    if (frontend->compositor && frontend->shm) {
        frontend->popup = typio_wl_popup_create(frontend);
        if (frontend->popup) {
            typio_log(TYPIO_LOG_INFO, "Popup candidate surface ready");
        } else {
            typio_log(TYPIO_LOG_WARNING,
                      "Failed to initialize popup candidate surface; keeping candidate state inline");
        }
    } else {
        typio_log(TYPIO_LOG_WARNING,
                  "Popup disabled: compositor=%p, shm=%p",
                  (void *)frontend->compositor, (void *)frontend->shm);
    }

    typio_log(TYPIO_LOG_INFO, "Wayland input method frontend initialized");

#ifdef HAVE_WHISPER
    frontend->voice = typio_voice_service_new(instance);
    if (frontend->voice && typio_voice_service_is_available(frontend->voice)) {
        typio_log(TYPIO_LOG_INFO, "Voice input service ready");
    } else if (frontend->voice) {
        typio_log(TYPIO_LOG_INFO, "Voice input service created but no model available");
    } else {
        typio_log(TYPIO_LOG_WARNING, "Failed to create voice input service");
    }
#endif

    return frontend;
}

int typio_wl_frontend_run(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->display) {
        return -1;
    }

    frontend->running = true;
    typio_log(TYPIO_LOG_INFO, "Starting Wayland event loop");

    int display_fd = wl_display_get_fd(frontend->display);

#ifdef HAVE_SYSTRAY
    int tray_fd = frontend->tray ? typio_tray_get_fd(frontend->tray) : -1;
#endif
#ifdef HAVE_STATUS_BUS
    int status_fd = frontend->status_bus ? typio_status_bus_get_fd(frontend->status_bus) : -1;
#endif
#ifdef HAVE_WHISPER
    int voice_fd = frontend->voice ? typio_voice_service_get_fd(frontend->voice) : -1;
#endif

    while (frontend->running) {
        /* Flush pending requests */
        while (wl_display_prepare_read(frontend->display) != 0) {
            wl_display_dispatch_pending(frontend->display);
        }

        if (wl_display_flush(frontend->display) < 0 && errno != EAGAIN) {
            typio_log(TYPIO_LOG_ERROR, "Wayland display flush failed: %s",
                      strerror(errno));
            wl_display_cancel_read(frontend->display);
            frontend->running = false;
            return -1;
        }

        /* Build poll set: wayland fd + optional tray/status/voice/repeat fds */
        struct pollfd fds[5];
        int nfds = 0;
        int idx_display = nfds;
        fds[nfds++] = (struct pollfd){ .fd = display_fd, .events = POLLIN };

#ifdef HAVE_SYSTRAY
        int idx_tray = -1;
        if (tray_fd >= 0) {
            idx_tray = nfds;
            fds[nfds++] = (struct pollfd){ .fd = tray_fd, .events = POLLIN };
        }
#endif

        int idx_status = -1;
#ifdef HAVE_STATUS_BUS
        if (status_fd >= 0) {
            idx_status = nfds;
            fds[nfds++] = (struct pollfd){ .fd = status_fd, .events = POLLIN };
        }
#endif

#ifdef HAVE_WHISPER
        int idx_voice = -1;
        if (voice_fd >= 0) {
            idx_voice = nfds;
            fds[nfds++] = (struct pollfd){ .fd = voice_fd, .events = POLLIN };
        }
#endif

        int idx_repeat = -1;
        int repeat_fd = frontend->keyboard ?
            typio_wl_keyboard_get_repeat_fd(frontend->keyboard) : -1;
        if (repeat_fd >= 0) {
            idx_repeat = nfds;
            fds[nfds++] = (struct pollfd){ .fd = repeat_fd, .events = POLLIN };
        }

        int ret = poll(fds, (nfds_t)nfds, 100);

        if (ret < 0) {
            if (errno == EINTR) {
                wl_display_cancel_read(frontend->display);
                continue;
            }
            typio_log(TYPIO_LOG_ERROR, "poll failed: %s", strerror(errno));
            wl_display_cancel_read(frontend->display);
            frontend->running = false;
            return -1;
        }

        if (ret == 0) {
            wl_display_cancel_read(frontend->display);
            continue;
        }

        /* Handle Wayland events */
        if (fds[idx_display].revents & POLLIN) {
            if (wl_display_read_events(frontend->display) < 0) {
                typio_log(TYPIO_LOG_ERROR, "Failed to read Wayland events: %s",
                          strerror(errno));
                frontend->running = false;
                return -1;
            }
            wl_display_dispatch_pending(frontend->display);
        } else {
            wl_display_cancel_read(frontend->display);
        }

        if (fds[idx_display].revents & (POLLERR | POLLHUP)) {
            typio_log(TYPIO_LOG_ERROR, "Wayland display connection error");
            frontend->running = false;
            return -1;
        }

#ifdef HAVE_SYSTRAY
        /* Handle tray D-Bus events */
        if (idx_tray >= 0 && fds[idx_tray].revents) {
            if (fds[idx_tray].revents & POLLIN) {
                typio_tray_dispatch(frontend->tray);
            }
            tray_fd = frontend->tray ? typio_tray_get_fd(frontend->tray) : -1;
        }
#endif

#ifdef HAVE_STATUS_BUS
        if (idx_status >= 0 && fds[idx_status].revents) {
            if (fds[idx_status].revents & POLLIN) {
                typio_status_bus_dispatch(frontend->status_bus);
            }
            status_fd = frontend->status_bus ? typio_status_bus_get_fd(frontend->status_bus) : -1;
        }
#endif

        /* Handle key repeat timer */
        if (idx_repeat >= 0 && (fds[idx_repeat].revents & POLLIN)) {
            typio_wl_keyboard_dispatch_repeat(frontend->keyboard);
        }

#ifdef HAVE_WHISPER
        /* Handle voice inference completion */
        if (idx_voice >= 0 && (fds[idx_voice].revents & POLLIN)) {
            TypioInputContext *ctx = frontend->session ?
                frontend->session->ctx : NULL;
            typio_voice_service_dispatch(frontend->voice, ctx);
            /* Clear the "Processing..." preedit */
            typio_wl_set_preedit(frontend, "", 0, 0);
            typio_wl_commit(frontend);
        }
#endif

        if (!frontend->running) {
            break;
        }
    }

    typio_log(TYPIO_LOG_INFO, "Wayland event loop stopped");
    return 0;
}

void typio_wl_frontend_stop(TypioWlFrontend *frontend) {
    if (frontend) {
        frontend->running = false;
    }
}

bool typio_wl_frontend_is_running(TypioWlFrontend *frontend) {
    return frontend && frontend->running;
}

void typio_wl_frontend_destroy(TypioWlFrontend *frontend) {
    if (!frontend) {
        return;
    }

    frontend->running = false;

    /* Clean up session */
    if (frontend->session) {
        typio_wl_session_destroy(frontend->session);
        frontend->session = NULL;
    }

    /* Clean up keyboard */
    if (frontend->keyboard) {
        typio_wl_keyboard_destroy(frontend->keyboard);
        frontend->keyboard = NULL;
    }

    if (frontend->popup) {
        typio_wl_popup_destroy(frontend->popup);
        frontend->popup = NULL;
    }

#ifdef HAVE_WHISPER
    if (frontend->voice) {
        typio_voice_service_free(frontend->voice);
        frontend->voice = NULL;
    }
#endif

    /* Clean up Wayland objects */
    if (frontend->virtual_keyboard) {
        zwp_virtual_keyboard_v1_destroy(frontend->virtual_keyboard);
    }
    if (frontend->vk_manager) {
        zwp_virtual_keyboard_manager_v1_destroy(frontend->vk_manager);
    }
    if (frontend->input_method) {
        zwp_input_method_v2_destroy(frontend->input_method);
    }
    if (frontend->im_manager) {
        zwp_input_method_manager_v2_destroy(frontend->im_manager);
    }
    if (frontend->seat) {
        wl_seat_destroy(frontend->seat);
    }
    if (frontend->shm) {
        wl_shm_destroy(frontend->shm);
    }
    if (frontend->compositor) {
        wl_compositor_destroy(frontend->compositor);
    }
    if (frontend->registry) {
        wl_registry_destroy(frontend->registry);
    }
    if (frontend->display) {
        wl_display_disconnect(frontend->display);
    }

    typio_log(TYPIO_LOG_INFO, "Wayland frontend destroyed");
    free(frontend);
}

const char *typio_wl_frontend_get_error(TypioWlFrontend *frontend) {
    if (!frontend || frontend->error_msg[0] == '\0') {
        return NULL;
    }
    return frontend->error_msg;
}

/* Registry handlers */
static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
    TypioWlFrontend *frontend = data;
    (void)version;

    if (strcmp(interface, zwp_input_method_manager_v2_interface.name) == 0) {
        frontend->im_manager = wl_registry_bind(registry, name,
                                                &zwp_input_method_manager_v2_interface, 1);
        typio_log(TYPIO_LOG_INFO, "Bound zwp_input_method_manager_v2");
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        frontend->compositor = wl_registry_bind(registry, name,
                                                &wl_compositor_interface, 4);
        typio_log(TYPIO_LOG_INFO, "Bound wl_compositor");
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        frontend->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        typio_log(TYPIO_LOG_INFO, "Bound wl_shm");
    } else if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        frontend->vk_manager = wl_registry_bind(registry, name,
                                                &zwp_virtual_keyboard_manager_v1_interface, 1);
        typio_log(TYPIO_LOG_INFO, "Bound zwp_virtual_keyboard_manager_v1");
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        /* Only bind first seat */
        if (!frontend->seat) {
            frontend->seat = wl_registry_bind(registry, name,
                                              &wl_seat_interface, 1);
            wl_seat_add_listener(frontend->seat, &seat_listener, frontend);
            typio_log(TYPIO_LOG_INFO, "Bound wl_seat");
        }
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
                                          uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
    /* Handle global removal if needed */
}

/* Seat handlers */
static void seat_handle_capabilities(void *data, struct wl_seat *seat,
                                     uint32_t capabilities) {
    (void)data;
    (void)seat;
    typio_log(TYPIO_LOG_DEBUG, "Seat capabilities: 0x%x", capabilities);
}

static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data;
    (void)seat;
    typio_log(TYPIO_LOG_DEBUG, "Seat name: %s", name);
}

void typio_wl_frontend_set_tray(TypioWlFrontend *frontend, void *tray) {
#ifdef HAVE_SYSTRAY
    if (frontend) {
        frontend->tray = (TypioTray *)tray;
    }
#else
    (void)frontend;
    (void)tray;
#endif
}

void typio_wl_frontend_set_status_bus(TypioWlFrontend *frontend, void *status_bus) {
#ifdef HAVE_STATUS_BUS
    if (frontend) {
        frontend->status_bus = (TypioStatusBus *)status_bus;
    }
#else
    (void)frontend;
    (void)status_bus;
#endif
}
