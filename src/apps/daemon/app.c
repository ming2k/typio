#include "app.h"

#include "typio/config.h"
#include "typio/engine_manager.h"
#include "typio/engine.h"
#include "typio/typio.h"
#include "typio_build_config.h"
#include "utils/log.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static TypioDaemonApp *g_active_app = nullptr;

#ifdef HAVE_SYSTRAY
static void typio_daemon_update_tray_engine_status(TypioDaemonApp *app);
#endif
#ifdef HAVE_STATUS_BUS
static void typio_daemon_update_status_bus_state(TypioDaemonApp *app);
#endif

static void typio_daemon_signal_handler([[maybe_unused]] int sig) {
    if (g_active_app) {
        g_active_app->shutdown_requested_by_signal = true;
    }
#ifdef HAVE_WAYLAND
    if (g_active_app && g_active_app->wl_frontend) {
        typio_wl_frontend_stop(g_active_app->wl_frontend);
    }
#endif
}

static void typio_daemon_log_callback(TypioLogLevel level,
                                      const char *message,
                                      [[maybe_unused]] void *user_data) {
    const char *level_str;
    struct timespec ts;
    struct tm tm;
    char timebuf[sizeof("YYYY-MM-DD HH:MM:SS")];

    switch (level) {
        case TYPIO_LOG_DEBUG:
            level_str = "DEBUG";
            break;
        case TYPIO_LOG_INFO:
            level_str = "INFO";
            break;
        case TYPIO_LOG_WARNING:
            level_str = "WARN";
            break;
        case TYPIO_LOG_ERROR:
            level_str = "ERROR";
            break;
        default:
            level_str = "UNKNOWN";
            break;
    }

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0 &&
        localtime_r(&ts.tv_sec, &tm)) {
        if (strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm) == 0) {
            timebuf[0] = '\0';
        }
    } else {
        timebuf[0] = '\0';
    }

    if (timebuf[0]) {
        fprintf(stderr, "[%s] [typio] [%s] %s\n", timebuf, level_str, message);
    } else {
        fprintf(stderr, "[typio] [%s] %s\n", level_str, message);
    }
}

static void typio_daemon_request_stop(void *user_data) {
    TypioDaemonApp *app = user_data;

    if (!app) {
        return;
    }

#ifdef HAVE_WAYLAND
    if (app->wl_frontend) {
        typio_wl_frontend_stop(app->wl_frontend);
    }
#endif
}

static const char *typio_daemon_engine_display_name(const TypioEngine *engine) {
    if (!engine) {
        return nullptr;
    }

    return typio_engine_label_from_info(engine->info);
}

#ifdef HAVE_SYSTRAY
static void typio_daemon_update_tray_tooltip(TypioDaemonApp *app) {
    TypioEngineManager *manager;
    TypioEngine *keyboard;
    TypioEngine *voice;
    const char *keyboard_label;
    const char *voice_label;
    char description[256];

    if (!app || !app->tray || !app->instance) {
        return;
    }

    manager = typio_instance_get_engine_manager(app->instance);
    keyboard = manager ? typio_engine_manager_get_active(manager) : nullptr;
    voice = manager ? typio_engine_manager_get_active_voice(manager) : nullptr;
    keyboard_label = keyboard ? typio_daemon_engine_display_name(keyboard) : "Unavailable";
    voice_label = voice ? typio_daemon_engine_display_name(voice) : "Disabled";

    snprintf(description, sizeof(description),
             "Keyboard: %s\nVoice: %s",
             keyboard_label ? keyboard_label : "Unavailable",
             voice_label ? voice_label : "Disabled");
    typio_tray_set_tooltip(app->tray, "Typio", description);
}
#endif

static void typio_daemon_sync_runtime_surfaces(TypioDaemonApp *app) {
#ifdef HAVE_SYSTRAY
    typio_daemon_update_tray_engine_status(app);
#endif
#ifdef HAVE_STATUS_BUS
    typio_daemon_update_status_bus_state(app);
#endif
}

static void typio_daemon_print_startup_banner(TypioDaemonApp *app) {
    TypioEngineManager *manager;
    TypioEngine *active_keyboard;
    TypioEngine *active_voice;

    typio_log(TYPIO_LOG_INFO, "Starting %s", typio_build_display_string());
    printf("%s started\n", typio_build_display_string());
    printf("Configuration: %s\n", typio_instance_get_config_dir(app->instance));
    printf("Data: %s\n", typio_instance_get_data_dir(app->instance));

    manager = typio_instance_get_engine_manager(app->instance);
    active_keyboard = manager ? typio_engine_manager_get_active(manager) : nullptr;
    active_voice = manager ? typio_engine_manager_get_active_voice(manager) : nullptr;
    if (active_keyboard) {
        printf("Active keyboard engine: %s\n", typio_engine_get_name(active_keyboard));
    } else {
        printf("No active keyboard engine\n");
    }
    printf("Active voice engine: %s\n",
           active_voice ? typio_engine_get_name(active_voice) : "(disabled)");

    printf("\nPress Ctrl+C to exit\n\n");
}

#ifdef HAVE_SYSTRAY
static bool typio_daemon_write_rime_schema_state(TypioDaemonApp *app,
                                                 const char *schema_name) {
    if (!app || !app->instance || !schema_name || !*schema_name) {
        return false;
    }

    return typio_instance_set_rime_schema(app->instance, schema_name) == TYPIO_OK;
}

static void typio_daemon_update_tray_engine_status(TypioDaemonApp *app) {
    TypioEngineManager *manager;
    TypioEngine *active_keyboard;
    const char *engine_name;
    const char *icon_name;

    if (!app || !app->tray || !app->instance) {
        return;
    }

    manager = typio_instance_get_engine_manager(app->instance);
    active_keyboard = manager ? typio_engine_manager_get_active(manager) : nullptr;
    engine_name = active_keyboard ? typio_engine_get_name(active_keyboard) : nullptr;
    icon_name = typio_instance_get_last_status_icon(app->instance);
    if (!icon_name || !*icon_name) {
        icon_name = (active_keyboard && active_keyboard->info && active_keyboard->info->icon)
                        ? active_keyboard->info->icon
                        : "typio-keyboard";
    }
    typio_tray_set_icon(app->tray, icon_name);
    typio_tray_update_engine(app->tray, engine_name, active_keyboard != nullptr);
    typio_daemon_update_tray_tooltip(app);
}
#endif

#ifdef HAVE_STATUS_BUS
static void typio_daemon_update_status_bus_state(TypioDaemonApp *app) {
    if (app && app->status_bus) {
        typio_status_bus_emit_properties_changed(app->status_bus);
    }
}
#endif

static void typio_daemon_on_mode_change(TypioInstance *instance,
                                        const TypioEngineMode *mode,
                                        void *user_data) {
    TypioDaemonApp *app = user_data;

    (void) instance;

#ifdef HAVE_SYSTRAY
    if (app && app->tray && mode && mode->icon_name) {
        typio_tray_set_icon(app->tray, mode->icon_name);
    }
#endif
#ifdef HAVE_STATUS_BUS
    if (app && app->status_bus) {
        typio_status_bus_emit_properties_changed(app->status_bus);
    }
#endif
    (void) app;
    (void) mode;
}

static void typio_daemon_on_status_icon_change(TypioInstance *instance,
                                               const char *icon_name,
                                               void *user_data) {
    TypioDaemonApp *app = user_data;

    (void) instance;

#ifdef HAVE_SYSTRAY
    if (app && app->tray && icon_name) {
        typio_tray_set_icon(app->tray, icon_name);
    }
#else
    (void) app;
    (void) icon_name;
#endif
}

static void typio_daemon_on_engine_change(TypioInstance *instance,
                                          const TypioEngineInfo *engine,
                                          void *user_data) {
    TypioDaemonApp *app = user_data;
    TypioEngineManager *manager;
    TypioEngine *active;

    (void) instance;
    (void) engine;

    typio_daemon_sync_runtime_surfaces(app);
    if (!app || !app->instance) {
        return;
    }

    manager = typio_instance_get_engine_manager(app->instance);
    active = manager ? typio_engine_manager_get_active(manager) : nullptr;
    if (active) {
#ifdef HAVE_WAYLAND
        if (app && app->wl_frontend) {
            typio_wl_frontend_remember_active_engine(app->wl_frontend,
                                                     typio_engine_get_name(active));
        }
#endif
        typio_log(TYPIO_LOG_INFO, "Engine changed to: %s", typio_engine_get_name(active));
    }
}

static void typio_daemon_on_voice_engine_change(TypioInstance *instance,
                                                const TypioEngineInfo *engine,
                                                void *user_data) {
    TypioDaemonApp *app = user_data;

    (void) instance;

    typio_daemon_sync_runtime_surfaces(app);
    if (engine && engine->name) {
        typio_log(TYPIO_LOG_INFO, "Voice engine changed to: %s", engine->name);
    }
}

#ifdef HAVE_SYSTRAY
static void typio_daemon_tray_menu_callback([[maybe_unused]] TypioTray *tray,
                                            const char *action,
                                            void *user_data) {
    TypioDaemonApp *app = user_data;
    TypioEngineManager *manager;

    if (!app || !action) {
        return;
    }

    if (strcmp(action, "quit") == 0) {
#ifdef HAVE_WAYLAND
        if (app->wl_frontend) {
            typio_wl_frontend_stop(app->wl_frontend);
        }
#endif
        return;
    }

    if (strcmp(action, "restart") == 0) {
        app->restart_requested = true;
#ifdef HAVE_WAYLAND
        if (app->wl_frontend) {
            typio_wl_frontend_stop(app->wl_frontend);
        }
#endif
        return;
    }

    manager = typio_instance_get_engine_manager(app->instance);
    if (!manager) {
        return;
    }

    if (strcmp(action, "activate") == 0) {
        typio_engine_manager_next(manager);
        typio_log(TYPIO_LOG_INFO, "Switched to next engine");
        return;
    }

    if (strcmp(action, "scroll_up") == 0) {
        typio_engine_manager_prev(manager);
        return;
    }

    if (strcmp(action, "scroll_down") == 0) {
        typio_engine_manager_next(manager);
        return;
    }

    if (strncmp(action, "engine:", 7) == 0) {
        const char *engine_name = action + 7;

        if (typio_engine_manager_set_active(manager, engine_name) == TYPIO_OK) {
            typio_log(TYPIO_LOG_INFO, "Switched to engine: %s", engine_name);
        }
        return;
    }

    if (strncmp(action, "rime-schema:", 12) == 0) {
        const char *schema_name = action + 12;

        if (typio_daemon_write_rime_schema_state(app, schema_name) &&
            typio_instance_reload_config(app->instance) == TYPIO_OK) {
            typio_daemon_update_tray_engine_status(app);
#ifdef HAVE_STATUS_BUS
            typio_daemon_update_status_bus_state(app);
#endif
            typio_log(TYPIO_LOG_INFO, "Switched Rime schema to: %s", schema_name);
        } else {
            typio_log(TYPIO_LOG_ERROR, "Failed to update Rime schema: %s", schema_name);
        }
        return;
    }

    if (strcmp(action, "rime-reload") == 0) {
        if (typio_instance_reload_config(app->instance) == TYPIO_OK) {
            typio_daemon_update_tray_engine_status(app);
#ifdef HAVE_STATUS_BUS
            typio_daemon_update_status_bus_state(app);
#endif
            typio_log(TYPIO_LOG_INFO, "Reloaded Rime configuration");
        } else {
            typio_log(TYPIO_LOG_ERROR, "Failed to reload Rime configuration");
        }
    }
}
#endif

static void typio_daemon_install_signal_handlers(TypioDaemonApp *app) {
    g_active_app = app;
    signal(SIGINT, typio_daemon_signal_handler);
    signal(SIGTERM, typio_daemon_signal_handler);
}

static void typio_daemon_configure_recent_log_dump(TypioDaemonApp *app) {
    const char *state_dir;

    if (!app || !app->instance) {
        return;
    }

    state_dir = typio_instance_get_state_dir(app->instance);
    if (!state_dir || !*state_dir) {
        return;
    }

    if (snprintf(app->recent_log_dump_path, sizeof(app->recent_log_dump_path),
                 "%s/%s", state_dir, "typio-recent.log") >=
        (int)sizeof(app->recent_log_dump_path)) {
        app->recent_log_dump_path[0] = '\0';
        return;
    }

    typio_log_set_recent_dump_path(app->recent_log_dump_path);
}

bool typio_daemon_app_init(TypioDaemonApp *app,
                           const TypioInstanceConfig *config,
                           bool verbose,
                           char *argv[]) {
    TypioInstanceConfig instance_config = {};
    TypioResult result;

    if (!app) {
        return false;
    }

    memset(app, 0, sizeof(*app));
    app->argv = argv;

    if (config) {
        instance_config = *config;
    }
    instance_config.log_callback = typio_daemon_log_callback;
    typio_log_set_level(verbose ? TYPIO_LOG_DEBUG : TYPIO_LOG_INFO);

    app->instance = typio_instance_new_with_config(&instance_config);
    if (!app->instance) {
        fprintf(stderr, "Failed to create Typio instance\n");
        return false;
    }

    result = typio_instance_init(app->instance);
    if (result != TYPIO_OK) {
        fprintf(stderr, "Failed to initialize Typio instance: %d\n", result);
        typio_instance_free(app->instance);
        app->instance = nullptr;
        return false;
    }

    typio_daemon_configure_recent_log_dump(app);

    return true;
}

void typio_daemon_app_list_engines(TypioDaemonApp *app) {
    TypioEngineManager *manager;
    size_t count;
    const char **engines;

    if (!app || !app->instance) {
        printf("No engine manager available\n");
        return;
    }

    manager = typio_instance_get_engine_manager(app->instance);
    if (!manager) {
        printf("No engine manager available\n");
        return;
    }

    engines = typio_engine_manager_list(manager, &count);
    printf("Available engines (%zu):\n\n", count);

    for (size_t i = 0; i < count; i++) {
        const TypioEngineInfo *info = typio_engine_manager_get_info(manager, engines[i]);

        if (!info) {
            continue;
        }

        printf("  %s\n", info->name);
        printf("    Display name: %s\n", info->display_name);
        printf("    Description:  %s\n", info->description);
        printf("    Version:      %s\n", info->version);
        printf("    Type:         %s\n",
               info->type == TYPIO_ENGINE_TYPE_VOICE ? "Voice" : "Keyboard");
        printf("    Language:     %s\n", info->language);
        printf("\n");
    }
}

static void typio_daemon_init_status_bus(TypioDaemonApp *app) {
#ifdef HAVE_STATUS_BUS
    app->status_bus = typio_status_bus_new(app->instance);
    if (app->status_bus) {
        typio_status_bus_set_stop_callback(app->status_bus,
                                           typio_daemon_request_stop,
                                           app);
        printf("D-Bus status interface initialized\n");
    } else {
        printf("D-Bus status interface not available\n");
    }
#else
    (void) app;
#endif
}

static void typio_daemon_init_tray(TypioDaemonApp *app) {
#ifdef HAVE_SYSTRAY
    TypioTrayConfig tray_config = {
        .icon_name = "typio-keyboard",
        .tooltip = "Typio Input Method",
        .menu_callback = typio_daemon_tray_menu_callback,
        .user_data = app,
    };

    app->tray = typio_tray_new(app->instance, &tray_config);
    if (app->tray && typio_tray_is_registered(app->tray)) {
        typio_daemon_update_tray_engine_status(app);
        printf("System tray initialized\n");
    } else if (app->tray) {
        typio_daemon_update_tray_engine_status(app);
        printf("System tray pending (StatusNotifierWatcher not running yet)\n");
    } else {
        printf("System tray not available (StatusNotifierWatcher may not be running)\n");
    }
#else
    (void) app;
#endif
}

static void typio_daemon_destroy_runtime_services(TypioDaemonApp *app) {
#ifdef HAVE_SYSTRAY
    if (app->tray) {
        typio_tray_destroy(app->tray);
        app->tray = nullptr;
    }
#endif
#ifdef HAVE_STATUS_BUS
    if (app->status_bus) {
        typio_status_bus_destroy(app->status_bus);
        app->status_bus = nullptr;
    }
#endif
}

static int typio_daemon_run_wayland(TypioDaemonApp *app) {
#ifdef HAVE_WAYLAND
    int wl_result;
    const char *wl_error;

    typio_instance_set_engine_changed_callback(app->instance,
                                               typio_daemon_on_engine_change,
                                               app);
    typio_instance_set_voice_engine_changed_callback(app->instance,
                                                     typio_daemon_on_voice_engine_change,
                                                     app);
    typio_instance_set_status_icon_changed_callback(app->instance,
                                                    typio_daemon_on_status_icon_change,
                                                    app);
    typio_instance_set_mode_changed_callback(app->instance,
                                              typio_daemon_on_mode_change,
                                              app);
    typio_daemon_sync_runtime_surfaces(app);

    app->wl_frontend = typio_wl_frontend_new(app->instance, nullptr);
    if (!app->wl_frontend) {
        fprintf(stderr, "Failed to create Wayland frontend\n");
        fprintf(stderr, "Make sure the session provides zwp_input_method_manager_v2 and a working text-input-v3 path\n");
        return 1;
    }

#ifdef HAVE_SYSTRAY
    if (app->tray) {
        typio_wl_frontend_set_tray(app->wl_frontend, app->tray);
    }
#endif
#ifdef HAVE_STATUS_BUS
    if (app->status_bus) {
        typio_wl_frontend_set_status_bus(app->wl_frontend, app->status_bus);
    }
#endif

    printf("Wayland input method frontend started\n");

    wl_result = typio_wl_frontend_run(app->wl_frontend);
    wl_error = typio_wl_frontend_get_error(app->wl_frontend);
    if (wl_error) {
        fprintf(stderr, "Wayland error: %s\n", wl_error);
    }

    typio_wl_frontend_destroy(app->wl_frontend);
    app->wl_frontend = nullptr;

    return wl_result < 0 ? 1 : 0;
#else
    (void) app;
    fprintf(stderr, "This build does not include the Wayland frontend.\n");
    fprintf(stderr, "Reconfigure with ENABLE_WAYLAND=ON to run Typio.\n");
    return 1;
#endif
}

int typio_daemon_app_run(TypioDaemonApp *app) {
    int exit_code;

    if (!app || !app->instance) {
        return 1;
    }

    typio_daemon_install_signal_handlers(app);
    typio_daemon_print_startup_banner(app);
    typio_daemon_init_status_bus(app);
    typio_daemon_init_tray(app);
    exit_code = typio_daemon_run_wayland(app);

    if (exit_code == 0) {
        printf("\nShutting down...\n");
    }

    return exit_code;
}

void typio_daemon_app_shutdown(TypioDaemonApp *app) {
    if (!app) {
        return;
    }

    if (g_active_app == app) {
        g_active_app = nullptr;
    }

    typio_daemon_destroy_runtime_services(app);
    if (app->instance) {
        typio_instance_free(app->instance);
        app->instance = nullptr;
    }
}

int typio_daemon_app_finish(TypioDaemonApp *app, int exit_code) {
    if (!app) {
        return exit_code;
    }

    if ((exit_code != 0 || app->shutdown_requested_by_signal) &&
        app->recent_log_dump_path[0] != '\0') {
        typio_log_dump_recent_to_configured_path(
            app->shutdown_requested_by_signal ? "signal shutdown" : "non-zero exit");
    }

    if (app->restart_requested && exit_code == 0) {
        printf("Restarting...\n");
        execv(app->argv[0], app->argv);
        perror("execv");
        return 1;
    }

    if (exit_code == 0) {
        printf("Goodbye!\n");
    }

    return exit_code;
}

#ifdef TYPIO_DAEMON_TEST
void typio_daemon_test_update_tray_engine_status(TypioDaemonApp *app) {
#ifdef HAVE_SYSTRAY
    typio_daemon_update_tray_engine_status(app);
#else
    (void)app;
#endif
}

void typio_daemon_test_on_engine_change(TypioInstance *instance,
                                        const TypioEngineInfo *engine,
                                        void *user_data) {
    typio_daemon_on_engine_change(instance, engine, user_data);
}

void typio_daemon_test_on_voice_engine_change(TypioInstance *instance,
                                              const TypioEngineInfo *engine,
                                              void *user_data) {
    typio_daemon_on_voice_engine_change(instance, engine, user_data);
}

void typio_daemon_test_on_status_icon_change(TypioInstance *instance,
                                             const char *icon_name,
                                             void *user_data) {
    typio_daemon_on_status_icon_change(instance, icon_name, user_data);
}
#endif
