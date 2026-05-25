/**
 * @file wl_frontend.c
 * @brief Wayland input method frontend implementation
 */

#include "wl_frontend.h"
#include "identity.h"
#include "key_tracking.h"
#include "monotonic_time.h"
#include "reconnect_backoff.h"
#include "wl_frontend_internal.h"
#include "aux_adapters.h"
#include "ipc/ipc_bus.h"
#include "typio/typio.h"
#include "typio/log.h"
#include "typio/string.h"

#include <time.h>
#ifdef HAVE_VOICE
#include "typio/voice.h"
#include "../voice/pw_capture.h"
#endif

#include <wayland-client.h>
#include <inttypes.h>
#include <signal.h>
#include <errno.h>
#include <sys/inotify.h>
#include <sys/timerfd.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

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

static uint32_t frontend_runtime_age_ms(uint64_t now_ms, uint64_t since_ms) {
    if (since_ms == 0 || now_ms <= since_ms) return 0;
    uint64_t delta = now_ms - since_ms;
    return (delta > UINT32_MAX) ? UINT32_MAX : (uint32_t)delta;
}

static int32_t frontend_runtime_deadline_remaining_ms(uint64_t now_ms,
                                                      uint64_t deadline_ms) {
    if (deadline_ms == 0) return 0;
    int64_t delta = (int64_t)deadline_ms - (int64_t)now_ms;
    if (delta > INT32_MAX) return INT32_MAX;
    if (delta < INT32_MIN) return INT32_MIN;
    return (int32_t)delta;
}

static void frontend_fill_runtime_state(void *user_data,
                                        TypioStatusRuntimeState *state) {
    TypioWlFrontend *frontend = user_data;
    if (!frontend || !state) return;

    uint64_t now_ms = typio_wl_monotonic_ms();
    state->frontend_backend = "wayland";
    state->lifecycle_phase = typio_wl_lifecycle_phase_name(frontend->lifecycle_phase);
    state->virtual_keyboard_state = typio_wl_vk_state_name(frontend->virtual_keyboard_state);
    state->keyboard_grab_active = frontend->keyboard && frontend->keyboard->grab;
    state->virtual_keyboard_has_keymap = frontend->virtual_keyboard_has_keymap;
    state->watchdog_armed = atomic_load(&frontend->watchdog_armed);
    state->active_key_generation = frontend->active_key_generation;
    state->virtual_keyboard_keymap_generation = frontend->virtual_keyboard_keymap_generation;
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
    if (!frontend) return nullptr;
    if (message) {
        snprintf(frontend->error_msg, sizeof(frontend->error_msg), "%s", message);
    }
    typio_wl_frontend_destroy(frontend);
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
    frontend->config_reload_timer_fd = -1;
    frontend->config_reload_pending = false;

    config_dir = typio_instance_get_config_dir(frontend->instance);
    if (!config_dir || !*config_dir) {
        return;
    }

    frontend->config_watch_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (frontend->config_watch_fd < 0) {
        typio_log(TYPIO_LOG_WARNING, "Failed to initialize configuration watch");
        return;
    }

    frontend->config_reload_timer_fd = timerfd_create(CLOCK_MONOTONIC,
                                                      TFD_NONBLOCK | TFD_CLOEXEC);
    if (frontend->config_reload_timer_fd < 0) {
        typio_log(TYPIO_LOG_WARNING,
                  "Failed to initialize configuration reload debounce timer");
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

#ifdef HAVE_VOICE
static void frontend_init_voice(TypioWlFrontend *frontend, TypioInstance *instance);

static void pw_audio_cb(const float *samples, size_t count, void *user_data) {
    typio_voice_session_feed_audio((TypioVoiceSession *)user_data, samples, count);
}

static void voice_event_cb(const TypioVoiceSessionEvent *event, void *user_data) {
    TypioWlFrontend *frontend = user_data;
    switch (event->type) {
    case TYPIO_VOICE_EVENT_STATE_CHANGE:
        switch (event->state) {
        case TYPIO_VOICE_STATE_LOADING:
            typio_wl_text_ui_backend_show_status(frontend->text_ui_backend,
                                                  "[Loading voice model...]");
            break;
        case TYPIO_VOICE_STATE_RECORDING:
            typio_wl_text_ui_backend_show_status(frontend->text_ui_backend,
                                                  "[Recording...]");
            break;
        case TYPIO_VOICE_STATE_PROCESSING:
            typio_wl_text_ui_backend_show_status(frontend->text_ui_backend,
                                                  "[Processing...]");
            break;
        case TYPIO_VOICE_STATE_IDLE:
        default:
            typio_wl_text_ui_backend_hide_status(frontend->text_ui_backend);
            break;
        }
        break;
    case TYPIO_VOICE_EVENT_RESULT:
        if (event->text && event->text[0]) {
            typio_log(TYPIO_LOG_INFO, "Voice raw: \"%s\"", event->text);
            typio_voice_filter_tags_inplace(event->text);
            const char *p = event->text;
            while (*p == ' ') p++;
            if (*p != '\0') {
                typio_log(TYPIO_LOG_INFO, "Voice result: \"%s\"", p);
                if (frontend->session && frontend->session->ctx) {
                    typio_input_context_commit(frontend->session->ctx, p);
                }
            }
            free(event->text);  /* ownership transferred to callback */
        }
        break;
    case TYPIO_VOICE_EVENT_ERROR:
        typio_wl_text_ui_backend_show_status(frontend->text_ui_backend,
                                              event->error);
        break;
    }
}
#endif
static void frontend_init_resume_signal(TypioWlFrontend *frontend);

/*
 * Bind (or rebind) every Wayland-derived object: connect the display, bind
 * globals, create the input method + virtual keyboard, and build the text
 * UI backend. Used both at startup and on reconnect; it touches nothing
 * that must survive a compositor restart (engine/session state, aux
 * handlers, config watch, resume signal). On a hard failure it writes
 * frontend->error_msg and returns false WITHOUT tearing down — the caller
 * decides whether to abort (startup) or retry (reconnect). Assumes all
 * Wayland pointers start NULL (true after calloc or unbind).
 */
static bool frontend_wayland_bind(TypioWlFrontend *frontend) {
    frontend->display = wl_display_connect(frontend->display_name);
    if (!frontend->display) {
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "Failed to connect to Wayland display: %s",
                 frontend->display_name ? frontend->display_name : "(default)");
        typio_log(TYPIO_LOG_ERROR, "%s", frontend->error_msg);
        return false;
    }
    typio_log(TYPIO_LOG_INFO, "Connected to Wayland display");

    frontend->registry = wl_display_get_registry(frontend->display);
    if (!frontend->registry) {
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "Failed to get Wayland registry");
        typio_log(TYPIO_LOG_ERROR, "%s", frontend->error_msg);
        return false;
    }
    wl_registry_add_listener(frontend->registry, &registry_listener, frontend);

    if (wl_display_roundtrip(frontend->display) < 0) {
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "Wayland roundtrip failed");
        typio_log(TYPIO_LOG_ERROR, "%s", frontend->error_msg);
        return false;
    }

    if (!frontend->im_manager) {
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "Session does not provide the required Wayland input-method/text-input protocol stack");
        typio_log(TYPIO_LOG_ERROR,
                  "Compositor does not support zwp_input_method_manager_v2");
        return false;
    }
    if (!frontend->seat) {
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "No seat available");
        typio_log(TYPIO_LOG_ERROR, "No seat available");
        return false;
    }
    if (!frontend->compositor || !frontend->shm) {
        typio_log(TYPIO_LOG_WARNING,
                  "Compositor missing wl_compositor or wl_shm; popup candidates disabled");
    }

    frontend->input_method = zwp_input_method_manager_v2_get_input_method(
        frontend->im_manager, frontend->seat);
    if (!frontend->input_method) {
        snprintf(frontend->error_msg, sizeof(frontend->error_msg),
                 "Failed to create input method");
        typio_log(TYPIO_LOG_ERROR, "Failed to create input method");
        return false;
    }
    typio_wl_input_method_setup(frontend);

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

    return true;
}

/*
 * Destroy every Wayland-derived object and disconnect the display, nulling
 * each pointer so a subsequent bind starts clean. Leaves engine/session,
 * aux handlers, config watch, and the resume signal untouched. Safe to call
 * with any subset already NULL.
 */
static void frontend_wayland_unbind(TypioWlFrontend *frontend) {
    if (frontend->keyboard) {
        typio_wl_keyboard_destroy(frontend->keyboard);
        frontend->keyboard = nullptr;
    }
    if (frontend->text_ui_backend) {
        typio_wl_text_ui_backend_destroy(frontend->text_ui_backend);
        frontend->text_ui_backend = nullptr;
    }
    if (frontend->virtual_keyboard) {
        typio_wl_vk_set_state(frontend, TYPIO_WL_VK_STATE_ABSENT,
                              "wayland unbind");
        zwp_virtual_keyboard_v1_destroy(frontend->virtual_keyboard);
        frontend->virtual_keyboard = nullptr;
    }
    if (frontend->vk_manager) {
        zwp_virtual_keyboard_manager_v1_destroy(frontend->vk_manager);
        frontend->vk_manager = nullptr;
    }
    if (frontend->input_method) {
        zwp_input_method_v2_destroy(frontend->input_method);
        frontend->input_method = nullptr;
    }
    if (frontend->im_manager) {
        zwp_input_method_manager_v2_destroy(frontend->im_manager);
        frontend->im_manager = nullptr;
    }
    if (frontend->seat) {
        wl_seat_destroy(frontend->seat);
        frontend->seat = nullptr;
    }
    if (frontend->shm) {
        wl_shm_destroy(frontend->shm);
        frontend->shm = nullptr;
    }
    if (frontend->viewporter) {
        wp_viewporter_destroy(frontend->viewporter);
        frontend->viewporter = nullptr;
    }
    if (frontend->fractional_scale_manager) {
        wp_fractional_scale_manager_v1_destroy(frontend->fractional_scale_manager);
        frontend->fractional_scale_manager = nullptr;
    }
    if (frontend->compositor) {
        wl_compositor_destroy(frontend->compositor);
        frontend->compositor = nullptr;
    }
    if (frontend->registry) {
        wl_registry_destroy(frontend->registry);
        frontend->registry = nullptr;
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
        frontend->display = nullptr;
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

    /* Remember the display name so the reconnect path can re-open it. */
    const char *display_name = config ? config->display_name : nullptr;
    frontend->display_name = display_name ? typio_strdup(display_name) : nullptr;

    /* Bind all Wayland-derived objects. */
    if (!frontend_wayland_bind(frontend)) {
        return frontend_init_failed(frontend, frontend->error_msg);
    }

    frontend->identity_provider = typio_wl_identity_provider_new(instance);

    typio_log(TYPIO_LOG_INFO, "Wayland input method frontend initialized");
    frontend_setup_config_watch(frontend);
    frontend_init_resume_signal(frontend);
#ifdef HAVE_VOICE
    frontend_init_voice(frontend, instance);
#endif
    return frontend;
}

static void frontend_on_resume(void *user_data, const char *reason,
                               uint64_t sleep_ms) {
    TypioWlFrontend *frontend = user_data;
    typio_wl_input_method_handle_resume(frontend, reason, sleep_ms);
}

static void frontend_init_resume_signal(TypioWlFrontend *frontend) {
    if (!frontend)
        return;

    frontend->resume_signal =
        typio_wl_resume_signal_create(frontend_on_resume, frontend);
    if (!frontend->resume_signal) {
        typio_log(TYPIO_LOG_WARNING, "Failed to create resume signal detector");
        return;
    }

    if (frontend->aux_handler_count <
        sizeof(frontend->aux_handlers) / sizeof(frontend->aux_handlers[0])) {
        TypioWlAuxHandler *h =
            typio_wl_aux_handler_for_resume_signal(frontend->resume_signal);
        if (h)
            frontend->aux_handlers[frontend->aux_handler_count++] = h;
    }
}

#ifdef HAVE_VOICE
static void frontend_init_voice(TypioWlFrontend *frontend,
                                 TypioInstance *instance) {
    TypioEngineManager *mgr = typio_instance_get_engine_manager(instance);
    TypioConfig *inst_config = typio_instance_get_config(instance);

#ifdef HAVE_WHISPER
    typio_engine_manager_register(mgr, typio_engine_create_whisper,
                                  typio_engine_get_info_whisper);
#endif
#ifdef HAVE_SHERPA_ONNX
    typio_engine_manager_register(mgr, typio_engine_create_sherpa,
                                  typio_engine_get_info_sherpa);
#endif

    const char *voice_engine = typio_config_get_string(inst_config,
                                                         "default_voice_engine",
                                                         nullptr);
    if (!voice_engine) {
#ifdef HAVE_WHISPER
        voice_engine = "whisper";
#elif defined(HAVE_SHERPA_ONNX)
        voice_engine = "sherpa-onnx";
#endif
    }
    if (voice_engine)
        typio_engine_manager_set_active_voice(mgr, voice_engine);

    TypioVoiceSession *voice = typio_voice_session_new(instance);
    if (voice) {
        TypioPwCapture *pw = typio_pw_capture_new(pw_audio_cb, voice);
        if (pw) {
            typio_voice_session_set_audio_source(voice,
                typio_pw_capture_as_audio_source(pw));
        }
        typio_voice_session_set_callback(voice, voice_event_cb, frontend);

        int idle_ms = typio_config_get_int(inst_config,
                                           "voice.unload_after_ms", 480000);
        if (idle_ms < 0) idle_ms = 0;
        typio_voice_session_set_idle_timeout_ms(voice, (uint32_t)idle_ms);
        typio_instance_set_voice_session(instance, voice);
    }
    if (voice && typio_voice_session_is_available(voice))
        typio_log(TYPIO_LOG_INFO, "Voice input service ready");
    else if (voice)
        typio_log(TYPIO_LOG_INFO, "Voice input service created but no model");
    else
        typio_log(TYPIO_LOG_WARNING, "Failed to create voice input service");

    if (voice) {
        size_t cap = sizeof(frontend->aux_handlers) / sizeof(frontend->aux_handlers[0]);
        if (frontend->aux_handler_count < cap) {
            TypioWlAuxHandler *h = typio_wl_aux_handler_for_voice(voice, frontend);
            if (h)
                frontend->aux_handlers[frontend->aux_handler_count++] = h;
        }
    }
}
#endif

void typio_wl_frontend_stop(TypioWlFrontend *frontend) {
    if (frontend) {
        frontend->running = false;
    }
}

bool typio_wl_frontend_reconnect(TypioWlFrontend *frontend) {
    uint32_t attempt = 0;

    if (!frontend)
        return false;

    typio_log(TYPIO_LOG_WARNING,
              "Wayland connection lost; attempting to reconnect");

    /* No event-loop progress happens during the blocking backoff, so park
     * the watchdog. Destroying the keyboard in unbind also disarms it; this
     * is belt-and-suspenders for the case where no grab existed. */
    atomic_store(&frontend->watchdog_armed, false);

    /* Drop every Wayland-derived object. Engine/session state, aux handlers,
     * config watch, and the resume signal are intentionally preserved, so an
     * in-flight composition survives the compositor restart. */
    frontend_wayland_unbind(frontend);

    /* Reset the input-method state to a clean disconnected baseline. A held
     * key during the outage produced no key-up, so fence the key generation
     * and clear tracking; the fresh grab after reconnect starts from zero. */
    frontend->lifecycle_phase = TYPIO_WL_PHASE_INACTIVE;
    frontend->pending_reactivation = false;
    frontend->reconcile_divergence_since_ms = 0;
    frontend->active_generation_owned_keys = false;
    frontend->carried_vk_modifiers = false;
    frontend->active_key_generation++;
    if (frontend->active_key_generation == 0)
        frontend->active_key_generation = 1;
    typio_wl_key_tracking_reset(frontend->key_states, TYPIO_WL_MAX_TRACKED_KEYS);
    typio_wl_key_tracking_reset_generations(frontend->key_generations,
                                            TYPIO_WL_MAX_TRACKED_KEYS);

    while (typio_wl_reconnect_should_retry(attempt)) {
        uint32_t delay_ms = typio_wl_reconnect_delay_ms(attempt);
        struct timespec ts = {
            .tv_sec = delay_ms / 1000,
            .tv_nsec = (long)(delay_ms % 1000) * 1000000L,
        };

        /* Honor a stop request that arrived before/while we were down. */
        if (!frontend->running)
            return false;

        nanosleep(&ts, nullptr);

        if (!frontend->running)
            return false;

        if (frontend_wayland_bind(frontend)) {
            typio_log(TYPIO_LOG_INFO,
                      "Reconnected to Wayland display after %u attempt(s)",
                      attempt + 1);
            /* The fatal path that called us may have cleared running; the
             * connection is healthy again, so resume the loop. */
            frontend->running = true;
            return true;
        }

        /* Bind left partial state; clear it before the next attempt. */
        frontend_wayland_unbind(frontend);
        attempt++;
    }

    typio_log(TYPIO_LOG_ERROR,
              "Failed to reconnect to Wayland display after %u attempts; exiting",
              attempt);
    return false;
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
    if (frontend->config_reload_timer_fd >= 0) {
        close(frontend->config_reload_timer_fd);
        frontend->config_reload_timer_fd = -1;
    }

    /* Clean up optional subsystems */
#ifdef HAVE_VOICE
    {
        TypioVoiceSession *voice = typio_instance_get_voice_session(frontend->instance);
        if (voice) {
            /* Detach audio source (daemon-owned) before freeing session. */
            typio_voice_session_set_audio_source(voice, NULL);
            typio_voice_session_free(voice);
            typio_instance_set_voice_session(frontend->instance, NULL);
        }
    }
#endif
    for (size_t i = 0; i < frontend->aux_handler_count; i++) {
        typio_wl_aux_handler_free(frontend->aux_handlers[i]);
        frontend->aux_handlers[i] = nullptr;
    }
    frontend->aux_handler_count = 0;

    /* The resume-signal aux handler wrapper is freed above, but it does not
     * own the detector itself (free_fn was null), so release it here. */
    if (frontend->resume_signal) {
        typio_wl_resume_signal_destroy(frontend->resume_signal);
        frontend->resume_signal = nullptr;
    }

    /* Clean up all Wayland-derived objects (keyboard already destroyed
     * above; unbind tolerates the NULL). */
    frontend_wayland_unbind(frontend);

    free(frontend->display_name);

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
        /* v6 enables wl_surface.preferred_buffer_scale (used as an
         * integer fallback when wp_fractional_scale_v1 is absent). */
        uint32_t want = version >= 6 ? 6u : version;
        frontend->compositor = wl_registry_bind(registry, name,
                                                &wl_compositor_interface, want);
        typio_log(TYPIO_LOG_INFO, "Bound wl_compositor v%u", want);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        frontend->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        typio_log(TYPIO_LOG_INFO, "Bound wl_shm");
    } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        frontend->fractional_scale_manager = wl_registry_bind(
            registry, name, &wp_fractional_scale_manager_v1_interface, 1);
        typio_log(TYPIO_LOG_INFO, "Bound wp_fractional_scale_manager_v1");
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        frontend->viewporter = wl_registry_bind(
            registry, name, &wp_viewporter_interface, 1);
        typio_log(TYPIO_LOG_INFO, "Bound wp_viewporter");
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
    if (!frontend || !tray) return;
    size_t cap = sizeof(frontend->aux_handlers) / sizeof(frontend->aux_handlers[0]);
    if (frontend->aux_handler_count >= cap) return;
    TypioWlAuxHandler *h = typio_wl_aux_handler_for_tray((TypioTray *)tray);
    if (h) frontend->aux_handlers[frontend->aux_handler_count++] = h;
#endif
}

void typio_wl_frontend_set_status_bus([[maybe_unused]] TypioWlFrontend *frontend,
                                      [[maybe_unused]] void *status_bus) {
#ifdef HAVE_STATUS_BUS
    if (!frontend || !status_bus) return;
    frontend->status_bus = (TypioStatusBus *)status_bus;
    typio_status_bus_set_runtime_state_callback(frontend->status_bus,
                                                frontend_fill_runtime_state,
                                                frontend);
    size_t cap = sizeof(frontend->aux_handlers) / sizeof(frontend->aux_handlers[0]);
    if (frontend->aux_handler_count >= cap) return;
    TypioWlAuxHandler *h =
        typio_wl_aux_handler_for_status_bus((TypioStatusBus *)status_bus);
    if (h) frontend->aux_handlers[frontend->aux_handler_count++] = h;
#endif
}

void typio_wl_frontend_set_ipc_bus(TypioWlFrontend *frontend, void *ipc_bus) {
    if (!frontend || !ipc_bus) return;
    frontend->ipc_bus = (struct TypioIpcBus *)ipc_bus;
    typio_ipc_bus_set_runtime_state_callback(frontend->ipc_bus,
                                              (TypioIpcBusRuntimeStateCallback)frontend_fill_runtime_state,
                                              frontend);
    size_t cap = sizeof(frontend->aux_handlers) / sizeof(frontend->aux_handlers[0]);
    if (frontend->aux_handler_count >= cap) return;
    TypioWlAuxHandler *h =
        typio_wl_aux_handler_for_ipc_bus((struct TypioIpcBus *)ipc_bus);
    if (h) frontend->aux_handlers[frontend->aux_handler_count++] = h;
}
