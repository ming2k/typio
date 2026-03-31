#include "wl_frontend_internal.h"

#include "typio/config.h"
#include "typio/engine.h"
#include "typio/engine_manager.h"
#include "typio/typio.h"
#include "utils/log.h"

#include <string.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>

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

void typio_wl_frontend_handle_config_watch(TypioWlFrontend *frontend) {
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
            runtime_config_refresh(frontend);
        }
    }
}
