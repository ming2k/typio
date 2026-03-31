/**
 * @file wl_frontend.c
 * @brief Wayland input method frontend implementation
 */

#include "wl_frontend.h"
#include "identity.h"
#include "monotonic_time.h"
#include "wl_frontend_internal.h"
#include "typio/typio.h"
#include "utils/log.h"

#include <wayland-client.h>
#include <inttypes.h>
#include <signal.h>
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
static void output_handle_geometry(void *data, struct wl_output *wl_output,
                                   int32_t x, int32_t y,
                                   int32_t physical_width,
                                   int32_t physical_height,
                                   int32_t subpixel, const char *make,
                                   const char *model, int32_t transform);
static void output_handle_mode(void *data, struct wl_output *wl_output,
                               uint32_t flags, int32_t width,
                               int32_t height, int32_t refresh);
static void output_handle_done(void *data, struct wl_output *wl_output);
static void output_handle_scale(void *data, struct wl_output *wl_output,
                                int32_t factor);

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

static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .done = output_handle_done,
    .scale = output_handle_scale,
};

#define TYPIO_WL_WATCHDOG_TIMEOUT_MS 5000
#define TYPIO_WL_WATCHDOG_POLL_US 200000

static void *frontend_watchdog_thread_main(void *data);
static void frontend_fill_runtime_state(void *user_data,
                                        TypioStatusRuntimeState *state);
static const char *frontend_loop_stage_name(TypioWlLoopStage stage);

void typio_wl_frontend_watchdog_heartbeat(TypioWlFrontend *frontend) {
    if (!frontend) {
        return;
    }

    atomic_store(&frontend->watchdog_heartbeat_ms, typio_wl_monotonic_ms());
}

static const char *frontend_loop_stage_name(TypioWlLoopStage stage) {
    switch (stage) {
        case TYPIO_WL_LOOP_STAGE_IDLE:
            return "idle";
        case TYPIO_WL_LOOP_STAGE_POPUP_UPDATE:
            return "popup_update";
        case TYPIO_WL_LOOP_STAGE_PREPARE_READ:
            return "prepare_read";
        case TYPIO_WL_LOOP_STAGE_FLUSH:
            return "flush";
        case TYPIO_WL_LOOP_STAGE_POLL:
            return "poll";
        case TYPIO_WL_LOOP_STAGE_READ_EVENTS:
            return "read_events";
        case TYPIO_WL_LOOP_STAGE_DISPATCH_PENDING:
            return "dispatch_pending";
        case TYPIO_WL_LOOP_STAGE_AUX_IO:
            return "aux_io";
        case TYPIO_WL_LOOP_STAGE_REPEAT:
            return "repeat";
        case TYPIO_WL_LOOP_STAGE_CONFIG_RELOAD:
            return "config_reload";
        default:
            return "unknown";
    }
}

void typio_wl_frontend_watchdog_set_stage(TypioWlFrontend *frontend,
                                          TypioWlLoopStage stage) {
    if (!frontend) {
        return;
    }

    atomic_store(&frontend->watchdog_loop_stage, (int)stage);
    atomic_store(&frontend->watchdog_stage_since_ms, typio_wl_monotonic_ms());
}

void typio_wl_frontend_watchdog_start(TypioWlFrontend *frontend) {
    if (!frontend || frontend->watchdog_thread_started) {
        return;
    }

    typio_wl_frontend_watchdog_heartbeat(frontend);
    typio_wl_frontend_watchdog_set_stage(frontend, TYPIO_WL_LOOP_STAGE_IDLE);
    atomic_store(&frontend->watchdog_stop, false);
    atomic_store(&frontend->watchdog_armed, false);
    if (pthread_create(&frontend->watchdog_thread, nullptr,
                       frontend_watchdog_thread_main, frontend) == 0) {
        frontend->watchdog_thread_started = true;
    } else {
        typio_log(TYPIO_LOG_WARNING, "Failed to start Wayland watchdog thread");
    }
}

void typio_wl_frontend_watchdog_stop(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->watchdog_thread_started) {
        return;
    }

    atomic_store(&frontend->watchdog_armed, false);
    atomic_store(&frontend->watchdog_stop, true);
    pthread_join(frontend->watchdog_thread, nullptr);
    frontend->watchdog_thread_started = false;
}

static uint32_t frontend_runtime_age_ms(uint64_t now_ms,
                                        uint64_t since_ms) {
    uint64_t delta;

    if (since_ms == 0 || now_ms <= since_ms) {
        return 0;
    }

    delta = now_ms - since_ms;
    if (delta > UINT32_MAX) {
        return UINT32_MAX;
    }

    return (uint32_t)delta;
}

static int32_t frontend_runtime_deadline_remaining_ms(uint64_t now_ms,
                                                      uint64_t deadline_ms) {
    int64_t delta;

    if (deadline_ms == 0) {
        return 0;
    }

    delta = (int64_t)deadline_ms - (int64_t)now_ms;
    if (delta > INT32_MAX) {
        return INT32_MAX;
    }
    if (delta < INT32_MIN) {
        return INT32_MIN;
    }

    return (int32_t)delta;
}

static void frontend_fill_runtime_state(void *user_data,
                                        TypioStatusRuntimeState *state) {
    TypioWlFrontend *frontend = user_data;
    uint64_t now_ms;

    if (!frontend || !state) {
        return;
    }

    now_ms = typio_wl_monotonic_ms();
    state->frontend_backend = "wayland";
    state->lifecycle_phase = typio_wl_lifecycle_phase_name(frontend->lifecycle_phase);
    state->virtual_keyboard_state = typio_wl_vk_state_name(frontend->virtual_keyboard_state);
    state->keyboard_grab_active = frontend->keyboard && frontend->keyboard->grab;
    state->virtual_keyboard_has_keymap = frontend->virtual_keyboard_has_keymap;
    state->watchdog_armed = atomic_load(&frontend->watchdog_armed);
    state->virtual_keyboard_drop_count =
        frontend->virtual_keyboard_drop_count > UINT32_MAX
            ? UINT32_MAX
            : (uint32_t)frontend->virtual_keyboard_drop_count;
    state->virtual_keyboard_state_age_ms =
        frontend_runtime_age_ms(now_ms, frontend->virtual_keyboard_state_since_ms);
    state->virtual_keyboard_keymap_age_ms =
        frontend_runtime_age_ms(now_ms, frontend->virtual_keyboard_last_keymap_ms);
    state->virtual_keyboard_forward_age_ms =
        frontend_runtime_age_ms(now_ms, frontend->virtual_keyboard_last_forward_ms);
    state->virtual_keyboard_keymap_deadline_remaining_ms =
        frontend_runtime_deadline_remaining_ms(now_ms,
                                               frontend->virtual_keyboard_keymap_deadline_ms);
}

void typio_wl_frontend_emit_runtime_state_changed(TypioWlFrontend *frontend) {
#ifdef HAVE_STATUS_BUS
    if (frontend && frontend->status_bus) {
        typio_status_bus_emit_properties_changed(frontend->status_bus);
    }
#else
    (void)frontend;
#endif
}

static TypioWlFrontend *frontend_init_failed(TypioWlFrontend *frontend,
                                             const char *message) {
    if (!frontend) {
        return nullptr;
    }

    if (message) {
        snprintf(frontend->error_msg, sizeof(frontend->error_msg), "%s", message);
    }

    typio_wl_frontend_destroy(frontend);
    return nullptr;
}

static void *frontend_watchdog_thread_main(void *data) {
    TypioWlFrontend *frontend = data;

    if (!frontend) {
        return nullptr;
    }

    while (!atomic_load(&frontend->watchdog_stop)) {
        struct timespec interval = {
            .tv_sec = 0,
            .tv_nsec = TYPIO_WL_WATCHDOG_POLL_US * 1000,
        };
        nanosleep(&interval, nullptr);

        if (!atomic_load(&frontend->watchdog_armed)) {
            continue;
        }

        uint64_t heartbeat_ms = atomic_load(&frontend->watchdog_heartbeat_ms);
        uint64_t stage_since_ms = atomic_load(&frontend->watchdog_stage_since_ms);
        uint64_t now_ms = typio_wl_monotonic_ms();
        TypioWlLoopStage stage =
            (TypioWlLoopStage)atomic_load(&frontend->watchdog_loop_stage);
        if (heartbeat_ms == 0 || now_ms < heartbeat_ms) {
            continue;
        }

        if ((now_ms - heartbeat_ms) < TYPIO_WL_WATCHDOG_TIMEOUT_MS) {
            continue;
        }

        dprintf(STDERR_FILENO,
                "[typio] [ERROR] Wayland frontend watchdog timeout: lag=%" PRIu64 "ms stage=%s stage_lag=%" PRIu64 "ms lifecycle=%s vk_state=%s popup_pending=%s, forcing process exit to release keyboard grab\n",
                now_ms - heartbeat_ms,
                frontend_loop_stage_name(stage),
                (stage_since_ms > 0 && now_ms >= stage_since_ms) ? (now_ms - stage_since_ms) : 0,
                typio_wl_lifecycle_phase_name(frontend->lifecycle_phase),
                typio_wl_vk_state_name(frontend->virtual_keyboard_state),
                frontend->popup_update_pending ? "yes" : "no");
        typio_log_dump_recent_to_configured_path("watchdog timeout");
        kill(getpid(), SIGKILL);
        break;
    }

    return nullptr;
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
    typio_wl_frontend_log_shortcuts(frontend, "Shortcuts:");

    /* Connect to Wayland display */
    const char *display_name = config ? config->display_name : nullptr;
    frontend->display = wl_display_connect(display_name);
    if (!frontend->display) {
        typio_log(TYPIO_LOG_ERROR, "Failed to connect to Wayland display");
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "Failed to connect to Wayland display: %s",
                 display_name ? display_name : "(default)");
        return frontend_init_failed(frontend, frontend->error_msg);
    }

    typio_log(TYPIO_LOG_INFO, "Connected to Wayland display");

    /* Get registry */
    frontend->registry = wl_display_get_registry(frontend->display);
    if (!frontend->registry) {
        typio_log(TYPIO_LOG_ERROR, "Failed to get Wayland registry");
        return frontend_init_failed(frontend, "Failed to get Wayland registry");
    }

    wl_registry_add_listener(frontend->registry, &registry_listener, frontend);

    /* Roundtrip to get globals */
    if (wl_display_roundtrip(frontend->display) < 0) {
        typio_log(TYPIO_LOG_ERROR, "Wayland roundtrip failed");
        return frontend_init_failed(frontend, "Wayland roundtrip failed");
    }

    /* Verify we have required interfaces */
    if (!frontend->im_manager) {
        typio_log(TYPIO_LOG_ERROR,
                  "Compositor does not support zwp_input_method_manager_v2");
        return frontend_init_failed(frontend,
                                    "Session does not provide the required Wayland input-method/text-input protocol stack");
    }

    if (!frontend->seat) {
        typio_log(TYPIO_LOG_ERROR, "No seat available");
        return frontend_init_failed(frontend, "No seat available");
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
        return frontend_init_failed(frontend, "Failed to create input method");
    }

    /* Set up input method listener */
    typio_wl_input_method_setup(frontend);
    frontend->identity_provider = typio_wl_identity_provider_new(instance);

    /* Create virtual keyboard for forwarding unhandled keys */
    if (frontend->vk_manager && frontend->seat) {
        frontend->virtual_keyboard =
            zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
                frontend->vk_manager, frontend->seat);
        if (frontend->virtual_keyboard) {
            typio_wl_vk_set_state(frontend, TYPIO_WL_VK_STATE_NEEDS_KEYMAP,
                                  "virtual keyboard object created");
            typio_log(TYPIO_LOG_INFO, "Virtual keyboard created for key forwarding");
        } else {
            typio_wl_vk_set_state(frontend, TYPIO_WL_VK_STATE_BROKEN,
                                  "create_virtual_keyboard returned null");
            typio_log(TYPIO_LOG_WARNING,
                      "Failed to create virtual keyboard; unhandled keys will be lost");
        }
    } else {
        typio_wl_vk_set_state(frontend, TYPIO_WL_VK_STATE_ABSENT,
                              "virtual keyboard manager unavailable");
        typio_log(TYPIO_LOG_WARNING,
                  "No virtual keyboard manager; unhandled keys will be lost");
    }

    frontend->text_ui_backend = typio_wl_text_ui_backend_create(frontend);
    if (frontend->text_ui_backend) {
        if (typio_wl_text_ui_backend_is_available(frontend->text_ui_backend)) {
            typio_log(TYPIO_LOG_INFO, "Candidate popup surface ready");
        } else if (!frontend->compositor || !frontend->shm) {
            typio_log(TYPIO_LOG_WARNING,
                      "Popup disabled: compositor=%p, shm=%p",
                      (void *)frontend->compositor, (void *)frontend->shm);
        } else {
            typio_log(TYPIO_LOG_WARNING,
                      "Failed to initialize candidate popup surface; keeping candidate state inline");
        }
    } else {
        typio_log(TYPIO_LOG_WARNING, "Failed to initialize text UI backend");
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
    typio_wl_frontend_watchdog_stop(frontend);

    /* Clean up session */
    if (frontend->session) {
        typio_wl_session_destroy(frontend->session);
        frontend->session = nullptr;
    }
    typio_wl_frontend_clear_identity(frontend);
    typio_wl_identity_provider_free(frontend->identity_provider);
    frontend->identity_provider = nullptr;

    /* Clean up keyboard */
    if (frontend->keyboard) {
        typio_wl_keyboard_destroy(frontend->keyboard);
        frontend->keyboard = nullptr;
    }

    if (frontend->text_ui_backend) {
        typio_wl_text_ui_backend_destroy(frontend->text_ui_backend);
        frontend->text_ui_backend = nullptr;
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
        typio_wl_vk_set_state(frontend, TYPIO_WL_VK_STATE_ABSENT,
                              "frontend shutdown");
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
    while (frontend->outputs) {
        TypioWlOutput *output = frontend->outputs;
        frontend->outputs = output->next;
        if (output->output) {
            wl_output_destroy(output->output);
        }
        free(output);
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
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        TypioWlOutput *output = calloc(1, sizeof(*output));
        if (!output) {
            typio_log(TYPIO_LOG_WARNING, "Failed to allocate wl_output tracking");
            return;
        }

        output->name = name;
        output->frontend = frontend;
        output->scale = 1;
        output->output = wl_registry_bind(registry, name, &wl_output_interface,
                                          version >= 2 ? 2u : version);
        if (!output->output) {
            free(output);
            typio_log(TYPIO_LOG_WARNING, "Failed to bind wl_output");
            return;
        }

        wl_output_add_listener(output->output, &output_listener, output);
        output->next = frontend->outputs;
        frontend->outputs = output;
        typio_log(TYPIO_LOG_INFO, "Bound wl_output %u", name);
    }
}

static void registry_handle_global_remove(void *data,
                                          [[maybe_unused]] struct wl_registry *registry,
                                          uint32_t name) {
    TypioWlFrontend *frontend = data;
    TypioWlOutput **link;

    if (!frontend) {
        return;
    }

    link = &frontend->outputs;
    while (*link) {
        TypioWlOutput *output = *link;
        if (output->name == name) {
            *link = output->next;
            if (output->output) {
                typio_wl_text_ui_backend_handle_output_change(frontend->text_ui_backend,
                                                              output->output);
                wl_output_destroy(output->output);
            }
            free(output);
            return;
        }
        link = &output->next;
    }
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

static void output_handle_geometry([[maybe_unused]] void *data,
                                   [[maybe_unused]] struct wl_output *wl_output,
                                   [[maybe_unused]] int32_t x,
                                   [[maybe_unused]] int32_t y,
                                   [[maybe_unused]] int32_t physical_width,
                                   [[maybe_unused]] int32_t physical_height,
                                   [[maybe_unused]] int32_t subpixel,
                                   [[maybe_unused]] const char *make,
                                   [[maybe_unused]] const char *model,
                                   [[maybe_unused]] int32_t transform) {
}

static void output_handle_mode([[maybe_unused]] void *data,
                               [[maybe_unused]] struct wl_output *wl_output,
                               [[maybe_unused]] uint32_t flags,
                               [[maybe_unused]] int32_t width,
                               [[maybe_unused]] int32_t height,
                               [[maybe_unused]] int32_t refresh) {
}

static void output_handle_done(void *data, [[maybe_unused]] struct wl_output *wl_output) {
    TypioWlOutput *output = data;

    if (!output) {
        return;
    }

    typio_log(TYPIO_LOG_DEBUG, "wl_output %u scale=%d", output->name, output->scale);
}

static void output_handle_scale(void *data, [[maybe_unused]] struct wl_output *wl_output,
                                int32_t factor) {
    TypioWlOutput *output = data;

    if (!output) {
        return;
    }

    output->scale = factor > 0 ? factor : 1;
    typio_wl_text_ui_backend_handle_output_change(output->frontend->text_ui_backend,
                                                  output->output);
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
        typio_status_bus_set_runtime_state_callback(frontend->status_bus,
                                                    frontend_fill_runtime_state,
                                                    frontend);
    }
#endif
}
