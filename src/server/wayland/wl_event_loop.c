#include "wl_frontend_internal.h"

#include "frontend_aux.h"
#include "text_ui_state.h"
#include "typio/input_context.h"
#include "utils/log.h"

#include <errno.h>
#include <poll.h>
#include <string.h>

typedef struct TypioWlLoopAuxFds {
    int display_fd;
#ifdef HAVE_SYSTRAY
    int tray_fd;
    bool tray_disabled;
#endif
#ifdef HAVE_STATUS_BUS
    int status_fd;
    bool status_disabled;
#endif
#ifdef HAVE_VOICE
    int voice_fd;
    bool voice_disabled;
#endif
} TypioWlLoopAuxFds;

static void event_loop_flush_pending_popup(TypioWlFrontend *frontend) {
    if (!frontend) {
        return;
    }

    if (!typio_wl_text_ui_should_flush_popup_update(
            frontend->popup_update_pending,
            frontend->session != nullptr,
            frontend->session && frontend->session->ctx,
            frontend->session && frontend->session->ctx &&
                typio_input_context_is_focused(frontend->session->ctx))) {
        return;
    }

    typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_POPUP_UPDATE);
    frontend->popup_update_pending = false;
    typio_wl_text_ui_backend_update(frontend->text_ui_backend, frontend->session->ctx);
    typio_wl_frontend_watchdog_heartbeat(frontend);
    typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_IDLE);
}

static int event_loop_prepare_and_flush(TypioWlFrontend *frontend) {
    while (wl_display_prepare_read(frontend->display) != 0) {
        typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_PREPARE_READ);
        wl_display_dispatch_pending(frontend->display);
        typio_wl_frontend_watchdog_heartbeat(frontend);
        typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_IDLE);
    }

    typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_FLUSH);
    if (wl_display_flush(frontend->display) < 0 && errno != EAGAIN) {
        typio_log(TYPIO_LOG_ERROR, "Wayland display flush failed: %s",
                  strerror(errno));
        wl_display_cancel_read(frontend->display);
        frontend->running = false;
        return -1;
    }

    typio_wl_frontend_watchdog_heartbeat(frontend);
    return 0;
}

static TypioWlLoopAuxFds event_loop_init_aux_fds(TypioWlFrontend *frontend) {
    TypioWlLoopAuxFds aux = {
        .display_fd = wl_display_get_fd(frontend->display),
    };

#ifdef HAVE_SYSTRAY
    aux.tray_fd = frontend->tray ? typio_tray_get_fd(frontend->tray) : -1;
    aux.tray_disabled = false;
#endif
#ifdef HAVE_STATUS_BUS
    aux.status_fd = frontend->status_bus ? typio_status_bus_get_fd(frontend->status_bus) : -1;
    aux.status_disabled = false;
#endif
#ifdef HAVE_VOICE
    aux.voice_fd = frontend->voice ? typio_voice_service_get_fd(frontend->voice) : -1;
    aux.voice_disabled = false;
#endif
    return aux;
}

static int event_loop_poll(TypioWlFrontend *frontend,
                           TypioWlLoopAuxFds *aux,
                           struct pollfd *fds,
                           int *idx_display,
                           int *idx_repeat,
                           int *idx_config
#ifdef HAVE_SYSTRAY
                           , int *idx_tray
#endif
#ifdef HAVE_STATUS_BUS
                           , int *idx_status
#endif
#ifdef HAVE_VOICE
                           , int *idx_voice
#endif
                           ) {
    int nfds = 0;

    *idx_display = nfds;
    fds[nfds++] = (struct pollfd){ .fd = aux->display_fd, .events = POLLIN };

#ifdef HAVE_SYSTRAY
    *idx_tray = -1;
    if (!aux->tray_disabled && aux->tray_fd >= 0) {
        *idx_tray = nfds;
        fds[nfds++] = (struct pollfd){ .fd = aux->tray_fd, .events = POLLIN };
    }
#endif
#ifdef HAVE_STATUS_BUS
    *idx_status = -1;
    if (!aux->status_disabled && aux->status_fd >= 0) {
        *idx_status = nfds;
        fds[nfds++] = (struct pollfd){ .fd = aux->status_fd, .events = POLLIN };
    }
#endif
#ifdef HAVE_VOICE
    *idx_voice = -1;
    if (!aux->voice_disabled && aux->voice_fd >= 0) {
        *idx_voice = nfds;
        fds[nfds++] = (struct pollfd){ .fd = aux->voice_fd, .events = POLLIN };
    }
#endif

    *idx_repeat = -1;
    if (frontend->keyboard) {
        int repeat_fd = typio_wl_keyboard_get_repeat_fd(frontend->keyboard);
        if (repeat_fd >= 0) {
            *idx_repeat = nfds;
            fds[nfds++] = (struct pollfd){ .fd = repeat_fd, .events = POLLIN };
        }
    }

    *idx_config = -1;
    if (frontend->config_watch_fd >= 0) {
        *idx_config = nfds;
        fds[nfds++] = (struct pollfd){ .fd = frontend->config_watch_fd, .events = POLLIN };
    }

    typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_POLL);
    return poll(fds, (nfds_t)nfds, 100);
}

static int event_loop_handle_wayland(TypioWlFrontend *frontend,
                                     const struct pollfd *display_fd) {
    if (display_fd->revents & POLLIN) {
        typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_READ_EVENTS);
        if (wl_display_read_events(frontend->display) < 0) {
            typio_log(TYPIO_LOG_ERROR, "Failed to read Wayland events: %s",
                      strerror(errno));
            frontend->running = false;
            return -1;
        }
        typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_DISPATCH_PENDING);
        wl_display_dispatch_pending(frontend->display);
        frontend->dispatch_epoch++;
        typio_wl_frontend_watchdog_heartbeat(frontend);
        typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_IDLE);
    } else {
        wl_display_cancel_read(frontend->display);
    }

    if (display_fd->revents & (POLLERR | POLLHUP)) {
        typio_log(TYPIO_LOG_ERROR, "Wayland display connection error");
        frontend->running = false;
        return -1;
    }

    return 0;
}

#ifdef HAVE_SYSTRAY
static void event_loop_handle_tray(TypioWlFrontend *frontend,
                                   TypioWlLoopAuxFds *aux,
                                   const struct pollfd *tray_fd) {
    if (!tray_fd || !tray_fd->revents) {
        return;
    }

    typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_AUX_IO);
    TypioWlAuxState tray_state = { .fd = aux->tray_fd, .disabled = aux->tray_disabled };
    if (typio_wl_aux_should_disable_on_revents(tray_fd->revents)) {
        typio_log(TYPIO_LOG_WARNING,
                  "Disabling tray integration after poll error: revents=0x%x",
                  tray_fd->revents);
        tray_state = typio_wl_aux_apply_transition(tray_state, tray_fd->revents, 0, -1);
    } else if (tray_fd->revents & POLLIN) {
        int dispatch_result = typio_tray_dispatch(frontend->tray);
        if (typio_wl_aux_should_disable_on_dispatch_result(dispatch_result)) {
            typio_log(TYPIO_LOG_WARNING,
                      "Disabling tray integration after dispatch failure");
            tray_state = typio_wl_aux_apply_transition(tray_state,
                                                       tray_fd->revents,
                                                       dispatch_result,
                                                       -1);
        } else {
            tray_state = typio_wl_aux_apply_transition(
                tray_state,
                tray_fd->revents,
                dispatch_result,
                frontend->tray ? typio_tray_get_fd(frontend->tray) : -1);
        }
    }
    aux->tray_fd = tray_state.fd;
    aux->tray_disabled = tray_state.disabled;
    typio_wl_frontend_watchdog_heartbeat(frontend);
    typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_IDLE);
}
#endif

#ifdef HAVE_STATUS_BUS
static void event_loop_handle_status_bus(TypioWlFrontend *frontend,
                                         TypioWlLoopAuxFds *aux,
                                         const struct pollfd *status_fd) {
    if (!status_fd || !status_fd->revents) {
        return;
    }

    typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_AUX_IO);
    TypioWlAuxState status_state = { .fd = aux->status_fd, .disabled = aux->status_disabled };
    if (typio_wl_aux_should_disable_on_revents(status_fd->revents)) {
        typio_log(TYPIO_LOG_WARNING,
                  "Disabling status bus after poll error: revents=0x%x",
                  status_fd->revents);
        status_state = typio_wl_aux_apply_transition(status_state,
                                                     status_fd->revents,
                                                     0,
                                                     -1);
    } else if (status_fd->revents & POLLIN) {
        int dispatch_result = typio_status_bus_dispatch(frontend->status_bus);
        if (typio_wl_aux_should_disable_on_dispatch_result(dispatch_result)) {
            typio_log(TYPIO_LOG_WARNING,
                      "Disabling status bus after dispatch failure");
            status_state = typio_wl_aux_apply_transition(status_state,
                                                         status_fd->revents,
                                                         dispatch_result,
                                                         -1);
        } else {
            status_state = typio_wl_aux_apply_transition(
                status_state,
                status_fd->revents,
                dispatch_result,
                frontend->status_bus ? typio_status_bus_get_fd(frontend->status_bus) : -1);
        }
    }
    aux->status_fd = status_state.fd;
    aux->status_disabled = status_state.disabled;
    typio_wl_frontend_watchdog_heartbeat(frontend);
    typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_IDLE);
}
#endif

#ifdef HAVE_VOICE
static void event_loop_handle_voice(TypioWlFrontend *frontend,
                                    TypioWlLoopAuxFds *aux,
                                    const struct pollfd *voice_fd) {
    if (!voice_fd || !voice_fd->revents) {
        return;
    }

    typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_AUX_IO);
    TypioWlAuxState voice_state = { .fd = aux->voice_fd, .disabled = aux->voice_disabled };
    if (typio_wl_aux_should_disable_on_revents(voice_fd->revents)) {
        typio_log(TYPIO_LOG_WARNING,
                  "Disabling voice eventfd after poll error: revents=0x%x",
                  voice_fd->revents);
        voice_state = typio_wl_aux_apply_transition(voice_state,
                                                    voice_fd->revents,
                                                    0,
                                                    -1);
    } else if (voice_fd->revents & POLLIN) {
        TypioInputContext *ctx = frontend->session ? frontend->session->ctx : nullptr;
        typio_voice_service_dispatch(frontend->voice, ctx);
        typio_wl_set_preedit(frontend, "", 0, 0);
        typio_wl_commit(frontend);
        voice_state = typio_wl_aux_apply_transition(
            voice_state,
            voice_fd->revents,
            0,
            frontend->voice ? typio_voice_service_get_fd(frontend->voice) : -1);
    }
    aux->voice_fd = voice_state.fd;
    aux->voice_disabled = voice_state.disabled;
    typio_wl_frontend_watchdog_heartbeat(frontend);
    typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_IDLE);
}
#endif

int typio_wl_frontend_run(TypioWlFrontend *frontend) {
    TypioWlLoopAuxFds aux;

    if (!frontend || !frontend->display) {
        return -1;
    }

    typio_wl_frontend_watchdog_start(frontend);
    frontend->running = true;
    typio_wl_frontend_watchdog_heartbeat(frontend);
    typio_log(TYPIO_LOG_INFO, "Starting Wayland event loop");
    aux = event_loop_init_aux_fds(frontend);

    while (frontend->running) {
        struct pollfd fds[6];
        int idx_display;
        int idx_repeat;
        int idx_config;
        int ret;
#ifdef HAVE_SYSTRAY
        int idx_tray;
#endif
#ifdef HAVE_STATUS_BUS
        int idx_status;
#endif
#ifdef HAVE_VOICE
        int idx_voice;
#endif

        typio_wl_frontend_watchdog_heartbeat(frontend);
        typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_IDLE);
        event_loop_flush_pending_popup(frontend);

        if (event_loop_prepare_and_flush(frontend) < 0) {
            return -1;
        }

        ret = event_loop_poll(frontend, &aux, fds, &idx_display, &idx_repeat, &idx_config
#ifdef HAVE_SYSTRAY
                              , &idx_tray
#endif
#ifdef HAVE_STATUS_BUS
                              , &idx_status
#endif
#ifdef HAVE_VOICE
                              , &idx_voice
#endif
        );

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
            typio_wl_vk_health_check(frontend);
            typio_wl_frontend_watchdog_heartbeat(frontend);
            continue;
        }

        if (event_loop_handle_wayland(frontend, &fds[idx_display]) < 0) {
            return -1;
        }

#ifdef HAVE_SYSTRAY
        if (idx_tray >= 0) {
            event_loop_handle_tray(frontend, &aux, &fds[idx_tray]);
        }
#endif
#ifdef HAVE_STATUS_BUS
        if (idx_status >= 0) {
            event_loop_handle_status_bus(frontend, &aux, &fds[idx_status]);
        }
#endif

        if (idx_repeat >= 0 && (fds[idx_repeat].revents & POLLIN)) {
            typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_REPEAT);
            typio_wl_keyboard_dispatch_repeat(frontend->keyboard);
            typio_wl_frontend_watchdog_heartbeat(frontend);
            typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_IDLE);
        }

#ifdef HAVE_VOICE
        if (idx_voice >= 0) {
            event_loop_handle_voice(frontend, &aux, &fds[idx_voice]);
        }
#endif

        if (idx_config >= 0 && (fds[idx_config].revents & POLLIN)) {
            typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_CONFIG_RELOAD);
            typio_wl_frontend_handle_config_watch(frontend);
            typio_wl_frontend_watchdog_heartbeat(frontend);
            typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_IDLE);
        }

        typio_wl_vk_health_check(frontend);
        if (!frontend->running) {
            break;
        }
        typio_wl_frontend_watchdog_heartbeat(frontend);
    }

    typio_log(TYPIO_LOG_INFO, "Wayland event loop stopped");
    return 0;
}
