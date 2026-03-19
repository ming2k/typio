/**
 * @file wl_frontend.c
 * @brief Wayland input method frontend implementation
 */

#include "wl_frontend.h"
#include "frontend_aux.h"
#include "wl_frontend_internal.h"
#include "typio/typio.h"
#include "utils/log.h"

#include <wayland-client.h>
#include <poll.h>
#include <errno.h>
#include <sys/inotify.h>
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

/**
 * Reload runtime configuration for all subsystems.
 *
 * Ordering contract — each step may depend on the previous:
 *   1. instance_reload_config — re-reads TOML, switches keyboard engine
 *   2. shortcuts — re-parse shortcut bindings from new config
 *   3. voice engine switch — check default_voice_engine, activate if changed
 *   4. voice engine reload — tell backend to re-read its config section
 *   5. voice service refresh — re-fetch engine reference, ensure audio infra
 *   6. status bus — broadcast property change to D-Bus listeners
 */
static void frontend_refresh_runtime_config(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->instance) {
        return;
    }

    typio_log(TYPIO_LOG_DEBUG, "Config reload: begin");

    /* 1. Core: re-read TOML, switch keyboard engine, reload its config */
    if (typio_instance_reload_config(frontend->instance) != TYPIO_OK) {
        typio_log(TYPIO_LOG_WARNING, "Config reload: failed to reload instance config");
        return;
    }

    TypioConfig *config = typio_instance_get_config(frontend->instance);

    /* 2. Shortcuts */
    typio_shortcut_config_load(&frontend->shortcuts, config);
    {
        char *sw = typio_shortcut_format(&frontend->shortcuts.switch_engine);
        char *ee = typio_shortcut_format(&frontend->shortcuts.emergency_exit);
        char *ptt = typio_shortcut_format(&frontend->shortcuts.voice_ptt);
        typio_log(TYPIO_LOG_INFO,
                  "Config reload: shortcuts switch_engine=%s emergency_exit=%s voice_ptt=%s",
                  sw ? sw : "(none)", ee ? ee : "(none)", ptt ? ptt : "(none)");
        free(sw);
        free(ee);
        free(ptt);
    }

#ifdef HAVE_VOICE
    {
        TypioEngineManager *mgr = typio_instance_get_engine_manager(frontend->instance);

        /* 3. Voice engine switch — check if configured engine changed */
        const char *configured_voice = typio_config_get_string(config,
                                                                "default_voice_engine",
                                                                NULL);
        if (!configured_voice) {
            configured_voice = typio_config_get_string(config,
                                                        "voice.backend", NULL);
        }
        if (configured_voice && *configured_voice) {
            TypioEngine *voice = typio_engine_manager_get_active_voice(mgr);
            const char *cur = voice ? typio_engine_get_name(voice) : NULL;
            if (!cur || strcmp(configured_voice, cur) != 0) {
                typio_engine_manager_set_active_voice(mgr, configured_voice);
            }
        }

        /* 4. Voice engine reload — re-read [engines.*] config */
        TypioEngine *voice = typio_engine_manager_get_active_voice(mgr);
        if (voice && voice->ops && voice->ops->reload_config) {
            voice->ops->reload_config(voice);
        }

        /* 5. Voice service refresh — re-fetch engine ref, ensure audio infra */
        if (frontend->voice) {
            typio_voice_service_reload(frontend->voice);
            typio_log(TYPIO_LOG_INFO,
                      "Config reload: voice available=%s",
                      typio_voice_service_is_available(frontend->voice) ? "yes" : "no");
        }
    }
#endif

#ifdef HAVE_STATUS_BUS
    /* 6. Status bus — notify external listeners */
    if (frontend->status_bus) {
        typio_status_bus_emit_properties_changed(frontend->status_bus);
    }
#endif

    typio_log(TYPIO_LOG_DEBUG, "Config reload: complete");
}

static void frontend_handle_config_watch(TypioWlFrontend *frontend) {
    char buffer[4096];
    ssize_t nread;

    if (!frontend || frontend->config_watch_fd < 0) {
        return;
    }

    while ((nread = read(frontend->config_watch_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t offset = 0;
        bool should_reload = false;

        while (offset < nread) {
            const struct inotify_event *event =
                (const struct inotify_event *)(buffer + offset);
            if ((event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE |
                                IN_DELETE_SELF | IN_MOVE_SELF | IN_ATTRIB)) != 0) {
                should_reload = true;
            }
            offset += (ssize_t)sizeof(struct inotify_event) + event->len;
        }

        if (should_reload) {
            frontend_refresh_runtime_config(frontend);
        }
    }
}

static void frontend_setup_config_watch(TypioWlFrontend *frontend) {
    char engines_dir[512];
    const char *config_dir;

    if (!frontend || !frontend->instance) {
        return;
    }

    frontend->config_watch_fd = -1;
    frontend->config_dir_watch = -1;
    frontend->config_engines_watch = -1;

    config_dir = typio_instance_get_config_dir(frontend->instance);
    if (!config_dir || !*config_dir) {
        return;
    }

    frontend->config_watch_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (frontend->config_watch_fd < 0) {
        typio_log(TYPIO_LOG_WARNING, "Failed to initialize configuration watch");
        return;
    }

    frontend->config_dir_watch = inotify_add_watch(frontend->config_watch_fd,
                                                   config_dir,
                                                   IN_CLOSE_WRITE | IN_MOVED_TO |
                                                   IN_CREATE | IN_DELETE |
                                                   IN_DELETE_SELF | IN_MOVE_SELF |
                                                   IN_ATTRIB);

    if (snprintf(engines_dir, sizeof(engines_dir), "%s/engines", config_dir) <
        (int)sizeof(engines_dir)) {
        frontend->config_engines_watch = inotify_add_watch(frontend->config_watch_fd,
                                                           engines_dir,
                                                           IN_CLOSE_WRITE | IN_MOVED_TO |
                                                           IN_CREATE | IN_DELETE |
                                                           IN_DELETE_SELF | IN_MOVE_SELF |
                                                           IN_ATTRIB);
    }
}

TypioWlFrontend *typio_wl_frontend_new(TypioInstance *instance,
                                        const TypioWlFrontendConfig *config) {
    if (!instance) {
        return nullptr;
    }

    TypioWlFrontend *frontend = calloc(1, sizeof(TypioWlFrontend));
    if (!frontend) {
        return nullptr;
    }

    frontend->instance = instance;

    /* Load shortcut bindings from config */
    typio_shortcut_config_load(&frontend->shortcuts,
                               typio_instance_get_config(instance));
    {
        char *se = typio_shortcut_format(&frontend->shortcuts.switch_engine);
        char *ee = typio_shortcut_format(&frontend->shortcuts.emergency_exit);
        char *ptt = typio_shortcut_format(&frontend->shortcuts.voice_ptt);
        typio_log(TYPIO_LOG_INFO,
                  "Shortcuts: switch_engine=%s, emergency_exit=%s, voice_ptt=%s",
                  se ? se : "(none)", ee ? ee : "(none)", ptt ? ptt : "(none)");
        free(se);
        free(ee);
        free(ptt);
    }

    /* Connect to Wayland display */
    const char *display_name = config ? config->display_name : nullptr;
    frontend->display = wl_display_connect(display_name);
    if (!frontend->display) {
        typio_log(TYPIO_LOG_ERROR, "Failed to connect to Wayland display");
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "Failed to connect to Wayland display: %s",
                 display_name ? display_name : "(default)");
        free(frontend);
        return nullptr;
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
        return nullptr;
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
        return nullptr;
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
        return nullptr;
    }

    if (!frontend->seat) {
        typio_log(TYPIO_LOG_ERROR, "No seat available");
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "No seat available");
        zwp_input_method_manager_v2_destroy(frontend->im_manager);
        wl_registry_destroy(frontend->registry);
        wl_display_disconnect(frontend->display);
        free(frontend);
        return nullptr;
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
        return nullptr;
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

#ifdef HAVE_VOICE
    {
        TypioEngineManager *mgr = typio_instance_get_engine_manager(instance);
        TypioConfig *inst_config = typio_instance_get_config(instance);

        /* Register voice engine adapters */
#ifdef HAVE_WHISPER
        typio_engine_manager_register(mgr, typio_engine_create_whisper,
                                      typio_engine_get_info_whisper);
#endif
#ifdef HAVE_SHERPA_ONNX
        typio_engine_manager_register(mgr, typio_engine_create_sherpa,
                                      typio_engine_get_info_sherpa);
#endif

        /* Determine which voice engine to activate */
        const char *voice_engine = typio_config_get_string(inst_config,
                                                            "default_voice_engine",
                                                            NULL);
        if (!voice_engine) {
            /* Legacy fallback: read voice.backend */
            voice_engine = typio_config_get_string(inst_config,
                                                    "voice.backend", NULL);
        }
        if (!voice_engine) {
            /* Default: try whisper, then sherpa-onnx */
#ifdef HAVE_WHISPER
            voice_engine = "whisper";
#elif defined(HAVE_SHERPA_ONNX)
            voice_engine = "sherpa-onnx";
#endif
        }
        if (voice_engine) {
            typio_engine_manager_set_active_voice(mgr, voice_engine);
        }
    }

    frontend->voice = typio_voice_service_new(instance);
    if (frontend->voice && typio_voice_service_is_available(frontend->voice)) {
        typio_log(TYPIO_LOG_INFO, "Voice input service ready");
    } else if (frontend->voice) {
        typio_log(TYPIO_LOG_INFO, "Voice input service created but no model available");
    } else {
        typio_log(TYPIO_LOG_WARNING, "Failed to create voice input service");
    }
#endif

    frontend_setup_config_watch(frontend);

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
#ifdef HAVE_VOICE
    int voice_fd = frontend->voice ? typio_voice_service_get_fd(frontend->voice) : -1;
    bool voice_disabled = false;
#endif
#ifdef HAVE_SYSTRAY
    bool tray_disabled = false;
#endif
#ifdef HAVE_STATUS_BUS
    bool status_disabled = false;
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
        struct pollfd fds[6];
        int nfds = 0;
        int idx_display = nfds;
        fds[nfds++] = (struct pollfd){ .fd = display_fd, .events = POLLIN };

#ifdef HAVE_SYSTRAY
        int idx_tray = -1;
        if (!tray_disabled && tray_fd >= 0) {
            idx_tray = nfds;
            fds[nfds++] = (struct pollfd){ .fd = tray_fd, .events = POLLIN };
        }
#endif

        int idx_status = -1;
#ifdef HAVE_STATUS_BUS
        if (!status_disabled && status_fd >= 0) {
            idx_status = nfds;
            fds[nfds++] = (struct pollfd){ .fd = status_fd, .events = POLLIN };
        }
#endif

#ifdef HAVE_VOICE
        int idx_voice = -1;
        if (!voice_disabled && voice_fd >= 0) {
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

        int idx_config = -1;
        if (frontend->config_watch_fd >= 0) {
            idx_config = nfds;
            fds[nfds++] = (struct pollfd){ .fd = frontend->config_watch_fd, .events = POLLIN };
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
            TypioWlAuxState tray_state = { .fd = tray_fd, .disabled = tray_disabled };
            if (typio_wl_aux_should_disable_on_revents(fds[idx_tray].revents)) {
                typio_log(TYPIO_LOG_WARNING,
                          "Disabling tray integration after poll error: revents=0x%x",
                          fds[idx_tray].revents);
                tray_state = typio_wl_aux_apply_transition(tray_state,
                                                           fds[idx_tray].revents,
                                                           0,
                                                           -1);
            } else if (fds[idx_tray].revents & POLLIN) {
                int dispatch_result = typio_tray_dispatch(frontend->tray);
                if (typio_wl_aux_should_disable_on_dispatch_result(dispatch_result)) {
                    typio_log(TYPIO_LOG_WARNING,
                              "Disabling tray integration after dispatch failure");
                    tray_state = typio_wl_aux_apply_transition(tray_state,
                                                               fds[idx_tray].revents,
                                                               dispatch_result,
                                                               -1);
                } else {
                    tray_state = typio_wl_aux_apply_transition(
                        tray_state,
                        fds[idx_tray].revents,
                        dispatch_result,
                        frontend->tray ? typio_tray_get_fd(frontend->tray) : -1);
                }
            }
            tray_fd = tray_state.fd;
            tray_disabled = tray_state.disabled;
        }
#endif

#ifdef HAVE_STATUS_BUS
        if (idx_status >= 0 && fds[idx_status].revents) {
            TypioWlAuxState status_state = { .fd = status_fd, .disabled = status_disabled };
            if (typio_wl_aux_should_disable_on_revents(fds[idx_status].revents)) {
                typio_log(TYPIO_LOG_WARNING,
                          "Disabling status bus after poll error: revents=0x%x",
                          fds[idx_status].revents);
                status_state = typio_wl_aux_apply_transition(status_state,
                                                             fds[idx_status].revents,
                                                             0,
                                                             -1);
            } else if (fds[idx_status].revents & POLLIN) {
                int dispatch_result =
                    typio_status_bus_dispatch(frontend->status_bus);
                if (typio_wl_aux_should_disable_on_dispatch_result(dispatch_result)) {
                    typio_log(TYPIO_LOG_WARNING,
                              "Disabling status bus after dispatch failure");
                    status_state = typio_wl_aux_apply_transition(status_state,
                                                                 fds[idx_status].revents,
                                                                 dispatch_result,
                                                                 -1);
                } else {
                    status_state = typio_wl_aux_apply_transition(
                        status_state,
                        fds[idx_status].revents,
                        dispatch_result,
                        frontend->status_bus ? typio_status_bus_get_fd(frontend->status_bus) : -1);
                }
            }
            status_fd = status_state.fd;
            status_disabled = status_state.disabled;
        }
#endif

        /* Handle key repeat timer */
        if (idx_repeat >= 0 && (fds[idx_repeat].revents & POLLIN)) {
            typio_wl_keyboard_dispatch_repeat(frontend->keyboard);
        }

#ifdef HAVE_VOICE
        /* Handle voice inference completion */
        if (idx_voice >= 0 && fds[idx_voice].revents) {
            TypioWlAuxState voice_state = { .fd = voice_fd, .disabled = voice_disabled };
            if (typio_wl_aux_should_disable_on_revents(fds[idx_voice].revents)) {
                typio_log(TYPIO_LOG_WARNING,
                          "Disabling voice eventfd after poll error: revents=0x%x",
                          fds[idx_voice].revents);
                voice_state = typio_wl_aux_apply_transition(voice_state,
                                                            fds[idx_voice].revents,
                                                            0,
                                                            -1);
            } else if (fds[idx_voice].revents & POLLIN) {
                TypioInputContext *ctx = frontend->session ?
                    frontend->session->ctx : nullptr;
                typio_voice_service_dispatch(frontend->voice, ctx);
                /* Clear the "Processing..." preedit */
                typio_wl_set_preedit(frontend, "", 0, 0);
                typio_wl_commit(frontend);
                voice_state = typio_wl_aux_apply_transition(
                    voice_state,
                    fds[idx_voice].revents,
                    0,
                    frontend->voice ? typio_voice_service_get_fd(frontend->voice) : -1);
            }
            voice_fd = voice_state.fd;
            voice_disabled = voice_state.disabled;
        }
#endif

        if (idx_config >= 0 && (fds[idx_config].revents & POLLIN)) {
            frontend_handle_config_watch(frontend);
        }

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
        frontend->session = nullptr;
    }

    /* Clean up keyboard */
    if (frontend->keyboard) {
        typio_wl_keyboard_destroy(frontend->keyboard);
        frontend->keyboard = nullptr;
    }

    if (frontend->popup) {
        typio_wl_popup_destroy(frontend->popup);
        frontend->popup = nullptr;
    }

    if (frontend->config_dir_watch >= 0 && frontend->config_watch_fd >= 0) {
        inotify_rm_watch(frontend->config_watch_fd, frontend->config_dir_watch);
    }
    if (frontend->config_engines_watch >= 0 && frontend->config_watch_fd >= 0) {
        inotify_rm_watch(frontend->config_watch_fd, frontend->config_engines_watch);
    }
    if (frontend->config_watch_fd >= 0) {
        close(frontend->config_watch_fd);
        frontend->config_watch_fd = -1;
    }

#ifdef HAVE_VOICE
    if (frontend->voice) {
        typio_voice_service_free(frontend->voice);
        frontend->voice = nullptr;
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
        return nullptr;
    }
    return frontend->error_msg;
}

/* Registry handlers */
static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   [[maybe_unused]] uint32_t version) {
    TypioWlFrontend *frontend = data;

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

static void registry_handle_global_remove([[maybe_unused]] void *data,
                                          [[maybe_unused]] struct wl_registry *registry,
                                          [[maybe_unused]] uint32_t name) {
    /* Handle global removal if needed */
}

/* Seat handlers */
static void seat_handle_capabilities([[maybe_unused]] void *data,
                                     [[maybe_unused]] struct wl_seat *seat,
                                     uint32_t capabilities) {
    typio_log(TYPIO_LOG_DEBUG, "Seat capabilities: 0x%x", capabilities);
}

static void seat_handle_name([[maybe_unused]] void *data,
                             [[maybe_unused]] struct wl_seat *seat,
                             const char *name) {
    typio_log(TYPIO_LOG_DEBUG, "Seat name: %s", name);
}

void typio_wl_frontend_set_tray([[maybe_unused]] TypioWlFrontend *frontend,
                                [[maybe_unused]] void *tray) {
#ifdef HAVE_SYSTRAY
    if (frontend) {
        frontend->tray = (TypioTray *)tray;
    }
#endif
}

void typio_wl_frontend_set_status_bus([[maybe_unused]] TypioWlFrontend *frontend,
                                      [[maybe_unused]] void *status_bus) {
#ifdef HAVE_STATUS_BUS
    if (frontend) {
        frontend->status_bus = (TypioStatusBus *)status_bus;
    }
#endif
}
