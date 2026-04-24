#include "wl_frontend_internal.h"

#include "typio/config.h"
#include "typio/engine.h"
#include "typio/engine_manager.h"
#include "typio/typio.h"
#include "utils/log.h"

#include <string.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define TYPIO_CONFIG_RELOAD_DEBOUNCE_MS 100

void typio_wl_frontend_log_shortcuts(TypioWlFrontend *frontend,
                                     const char *prefix) {
    char *switch_engine;
    char *emergency_exit;
    char *voice_ptt;

    if (!frontend || !prefix) {
        return;
    }

    switch_engine = typio_shortcut_format(&frontend->shortcuts.switch_engine);
    emergency_exit = typio_shortcut_format(&frontend->shortcuts.emergency_exit);
    voice_ptt = typio_shortcut_format(&frontend->shortcuts.voice_ptt);
    typio_log(TYPIO_LOG_INFO,
              "%s switch_engine=%s emergency_exit=%s voice_ptt=%s",
              prefix,
              switch_engine ? switch_engine : "(none)",
              emergency_exit ? emergency_exit : "(none)",
              voice_ptt ? voice_ptt : "(none)");
    free(switch_engine);
    free(emergency_exit);
    free(voice_ptt);
}

static void runtime_config_refresh(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->instance) {
        return;
    }

    typio_log(TYPIO_LOG_DEBUG, "Config reload: begin");
    if (typio_instance_reload_config(frontend->instance) != TYPIO_OK) {
        typio_log(TYPIO_LOG_WARNING, "Config reload: failed to reload instance config");
        return;
    }

    typio_wl_text_ui_backend_invalidate_config(frontend->text_ui_backend);

    TypioConfig *config = typio_instance_get_config(frontend->instance);
    typio_shortcut_config_load(&frontend->shortcuts, config);
    typio_wl_frontend_log_shortcuts(frontend, "Config reload: shortcuts");

#ifdef HAVE_VOICE
    {
        TypioEngineManager *mgr = typio_instance_get_engine_manager(frontend->instance);
        const char *configured_voice = typio_config_get_string(config,
                                                               "default_voice_engine",
                                                               NULL);
        if (configured_voice && *configured_voice) {
            TypioEngine *voice = typio_engine_manager_get_active_voice(mgr);
            const char *cur = voice ? typio_engine_get_name(voice) : NULL;
            if (!cur || strcmp(configured_voice, cur) != 0) {
                typio_engine_manager_set_active_voice(mgr, configured_voice);
            }
        }

        TypioEngine *voice = typio_engine_manager_get_active_voice(mgr);
        if (voice && voice->ops && voice->ops->reload_config) {
            voice->ops->reload_config(voice);
        }

        if (frontend->voice) {
            typio_voice_service_reload(frontend->voice);
            typio_log(TYPIO_LOG_INFO,
                      "Config reload: voice available=%s",
                      typio_voice_service_is_available(frontend->voice) ? "yes" : "no");
        }
    }
#endif

#ifdef HAVE_STATUS_BUS
    if (frontend->status_bus) {
        typio_status_bus_emit_properties_changed(frontend->status_bus);
    }
#endif

    typio_log(TYPIO_LOG_DEBUG, "Config reload: complete");
}

static void runtime_config_rearm_watch(TypioWlFrontend *frontend) {
    char engines_dir[512];
    const char *config_dir;

    if (!frontend || frontend->config_watch_fd < 0 || !frontend->instance) {
        return;
    }

    config_dir = typio_instance_get_config_dir(frontend->instance);
    if (!config_dir || !*config_dir) {
        return;
    }

    if (frontend->config_dir_watch >= 0) {
        inotify_rm_watch(frontend->config_watch_fd, frontend->config_dir_watch);
        frontend->config_dir_watch = -1;
    }
    if (frontend->config_engines_watch >= 0) {
        inotify_rm_watch(frontend->config_watch_fd, frontend->config_engines_watch);
        frontend->config_engines_watch = -1;
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

static void runtime_config_schedule_reload(TypioWlFrontend *frontend) {
    struct itimerspec timer = {};

    if (!frontend) {
        return;
    }

    frontend->config_reload_pending = true;
    if (frontend->config_reload_timer_fd < 0) {
        runtime_config_refresh(frontend);
        frontend->config_reload_pending = false;
        return;
    }

    timer.it_value.tv_sec = TYPIO_CONFIG_RELOAD_DEBOUNCE_MS / 1000;
    timer.it_value.tv_nsec =
        (long)(TYPIO_CONFIG_RELOAD_DEBOUNCE_MS % 1000) * 1000000L;
    if (timerfd_settime(frontend->config_reload_timer_fd, 0, &timer, NULL) != 0) {
        typio_log(TYPIO_LOG_WARNING,
                  "Config reload: failed to arm debounce timer; reloading immediately");
        runtime_config_refresh(frontend);
        frontend->config_reload_pending = false;
    }
}

void typio_wl_frontend_handle_config_watch(TypioWlFrontend *frontend) {
    char buffer[4096];
    ssize_t nread;
    bool should_reload = false;
    bool should_rearm = false;

    if (!frontend || frontend->config_watch_fd < 0) {
        return;
    }

    while ((nread = read(frontend->config_watch_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t offset = 0;

        while (offset < nread) {
            const struct inotify_event *event =
                (const struct inotify_event *)(buffer + offset);
            if ((event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE |
                                IN_DELETE_SELF | IN_MOVE_SELF | IN_ATTRIB)) != 0) {
                should_reload = true;
            }
            if ((event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0) {
                should_rearm = true;
            }
            offset += (ssize_t)sizeof(struct inotify_event) + event->len;
        }
    }

    if (should_rearm) {
        runtime_config_rearm_watch(frontend);
    }
    if (should_reload) {
        runtime_config_schedule_reload(frontend);
    }
}

int typio_wl_frontend_get_config_reload_fd(TypioWlFrontend *frontend) {
    return frontend ? frontend->config_reload_timer_fd : -1;
}

void typio_wl_frontend_dispatch_config_reload(TypioWlFrontend *frontend) {
    uint64_t expirations;

    if (!frontend || frontend->config_reload_timer_fd < 0) {
        return;
    }

    if (read(frontend->config_reload_timer_fd,
             &expirations,
             sizeof(expirations)) < 0) {
        return;
    }

    if (!frontend->config_reload_pending) {
        return;
    }

    frontend->config_reload_pending = false;
    runtime_config_refresh(frontend);
}
