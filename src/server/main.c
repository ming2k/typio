/**
 * @file main.c
 * @brief Typio Input Method Framework - Main Server
 *
 * This is the main entry point for the Typio input method server.
 */

#include "typio/typio.h"
#include "typio/config.h"
#include "typio_build_config.h"
#include "utils/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>

#ifdef HAVE_WAYLAND
#include "wayland/wl_frontend.h"
#endif

#ifdef HAVE_STATUS_BUS
#include "status/status.h"
#endif
#ifdef HAVE_SYSTRAY
#include "tray/tray.h"
#endif

static TypioInstance *g_instance = nullptr;
static char **g_argv = nullptr;
static bool g_restart_requested = false;
#ifdef HAVE_WAYLAND
static TypioWlFrontend *g_wl_frontend = nullptr;
#endif
#ifdef HAVE_STATUS_BUS
static TypioStatusBus *g_status_bus = nullptr;
#endif
#ifdef HAVE_SYSTRAY
static TypioTray *g_tray = nullptr;
#endif

static void signal_handler([[maybe_unused]] int sig) {
#ifdef HAVE_WAYLAND
    if (g_wl_frontend) {
        typio_wl_frontend_stop(g_wl_frontend);
    }
#endif
}

static void print_version(void) {
    printf("Typio Input Method Framework %s\n", TYPIO_VERSION);
    printf("An extensible input method framework supporting multiple engines\n");
}

static void print_help(const char *prog) {
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  -c, --config DIR    Configuration directory\n");
    printf("  -d, --data DIR      Data directory\n");
    printf("  -E, --engine-dir DIR Engine plugin directory\n");
    printf("  -e, --engine NAME   Default engine to use\n");
    printf("  -l, --list          List available engines and exit\n");
    printf("  -v, --verbose       Enable verbose logging\n");
    printf("  -h, --help          Show this help message\n");
    printf("  --version           Show version information\n");
}

#ifdef HAVE_SYSTRAY
static bool write_rime_schema_config(const char *schema_name) {
    const char *data_dir;
    char user_data_dir[512];
    TypioConfig *config;
    TypioResult result;

    if (!g_instance || !schema_name || !*schema_name) {
        return false;
    }

    data_dir = typio_instance_get_data_dir(g_instance);
    if (!data_dir) {
        return false;
    }

    if (snprintf(user_data_dir, sizeof(user_data_dir), "%s/rime", data_dir) >= (int)sizeof(user_data_dir)) {
        return false;
    }

    config = typio_instance_get_config(g_instance);
    if (!config) {
        return false;
    }

    typio_config_set_string(config, "engines.rime.schema", schema_name);
    if (!typio_config_has_key(config, "engines.rime.user_data_dir")) {
        typio_config_set_string(config, "engines.rime.user_data_dir", user_data_dir);
    }
    if (!typio_config_has_key(config, "engines.rime.popup_theme")) {
        typio_config_set_string(config, "engines.rime.popup_theme", "auto");
    }
    if (!typio_config_has_key(config, "engines.rime.candidate_layout")) {
        typio_config_set_string(config, "engines.rime.candidate_layout", "horizontal");
    }
    if (!typio_config_has_key(config, "engines.rime.font_size")) {
        typio_config_set_int(config, "engines.rime.font_size", 11);
    }

    result = typio_instance_save_config(g_instance);
    if (result != TYPIO_OK) {
        return false;
    }

    return typio_instance_reload_config(g_instance) == TYPIO_OK;
}
#endif

static void log_callback(TypioLogLevel level, const char *message, [[maybe_unused]] void *user_data) {
    const char *level_str;
    switch (level) {
        case TYPIO_LOG_DEBUG:   level_str = "DEBUG"; break;
        case TYPIO_LOG_INFO:    level_str = "INFO"; break;
        case TYPIO_LOG_WARNING: level_str = "WARN"; break;
        case TYPIO_LOG_ERROR:   level_str = "ERROR"; break;
        default: level_str = "UNKNOWN"; break;
    }

    fprintf(stderr, "[typio] [%s] %s\n", level_str, message);
}

static void list_engines(TypioInstance *instance) {
    TypioEngineManager *manager = typio_instance_get_engine_manager(instance);
    if (!manager) {
        printf("No engine manager available\n");
        return;
    }

    size_t count;
    const char **engines = typio_engine_manager_list(manager, &count);

    printf("Available engines (%zu):\n\n", count);

    for (size_t i = 0; i < count; i++) {
        const TypioEngineInfo *info = typio_engine_manager_get_info(manager, engines[i]);
        if (info) {
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
}

#ifdef HAVE_SYSTRAY
static void update_tray_engine_status(void) {
    if (!g_tray || !g_instance) return;

    TypioEngineManager *manager = typio_instance_get_engine_manager(g_instance);
    TypioEngine *active = typio_engine_manager_get_active(manager);
    const char *engine_name = active ? typio_engine_get_name(active) : nullptr;
    const char *icon_name = (active && active->info && active->info->icon) ?
                            active->info->icon : "typio-keyboard";
    typio_tray_set_icon(g_tray, icon_name);
    typio_tray_update_engine(g_tray, engine_name, active != nullptr);
}
#endif

#ifdef HAVE_STATUS_BUS
static void update_status_bus_state(void) {
    if (g_status_bus) {
        typio_status_bus_emit_properties_changed(g_status_bus);
    }
}
#endif

#ifdef HAVE_SYSTRAY
static void tray_menu_callback([[maybe_unused]] TypioTray *tray, const char *action, [[maybe_unused]] void *user_data) {
    if (strcmp(action, "quit") == 0) {
#ifdef HAVE_WAYLAND
        if (g_wl_frontend) {
            typio_wl_frontend_stop(g_wl_frontend);
        }
#endif
    } else if (strcmp(action, "restart") == 0) {
        g_restart_requested = true;
#ifdef HAVE_WAYLAND
        if (g_wl_frontend) {
            typio_wl_frontend_stop(g_wl_frontend);
        }
#endif
    } else if (strcmp(action, "activate") == 0) {
        /* Left-click: cycle to next engine */
        TypioEngineManager *manager = typio_instance_get_engine_manager(g_instance);
        typio_engine_manager_next(manager);
        typio_log(TYPIO_LOG_INFO, "Switched to next engine");
    } else if (strcmp(action, "scroll_up") == 0) {
        /* Scroll up: previous engine */
        TypioEngineManager *manager = typio_instance_get_engine_manager(g_instance);
        typio_engine_manager_prev(manager);
    } else if (strcmp(action, "scroll_down") == 0) {
        /* Scroll down: next engine */
        TypioEngineManager *manager = typio_instance_get_engine_manager(g_instance);
        typio_engine_manager_next(manager);
    } else if (strncmp(action, "engine:", 7) == 0) {
        /* Select specific engine: "engine:rime" or "engine:whisper" */
        const char *engine_name = action + 7;
        TypioEngineManager *manager = typio_instance_get_engine_manager(g_instance);
        if (typio_engine_manager_set_active(manager, engine_name) == TYPIO_OK) {
            typio_log(TYPIO_LOG_INFO, "Switched to engine: %s", engine_name);
        }
    } else if (strncmp(action, "rime-schema:", 12) == 0) {
        const char *schema_name = action + 12;
        if (write_rime_schema_config(schema_name) &&
            typio_instance_reload_config(g_instance) == TYPIO_OK) {
            update_tray_engine_status();
            update_status_bus_state();
            typio_log(TYPIO_LOG_INFO, "Switched Rime schema to: %s", schema_name);
        } else {
            typio_log(TYPIO_LOG_ERROR, "Failed to update Rime schema: %s", schema_name);
        }
    } else if (strcmp(action, "rime-reload") == 0) {
        if (typio_instance_reload_config(g_instance) == TYPIO_OK) {
            update_tray_engine_status();
            update_status_bus_state();
            typio_log(TYPIO_LOG_INFO, "Reloaded Rime configuration");
        } else {
            typio_log(TYPIO_LOG_ERROR, "Failed to reload Rime configuration");
        }
    }
}
#endif

static void on_status_icon_change([[maybe_unused]] TypioInstance *instance,
                                   [[maybe_unused]] const char *icon_name,
                                   [[maybe_unused]] void *user_data) {
#ifdef HAVE_SYSTRAY
    if (g_tray && icon_name) {
        typio_tray_set_icon(g_tray, icon_name);
    }
#endif
}

static void on_engine_change([[maybe_unused]] TypioInstance *instance,
                             [[maybe_unused]] const TypioEngineInfo *engine,
                             [[maybe_unused]] void *user_data) {
#ifdef HAVE_SYSTRAY
    update_tray_engine_status();
#endif
#ifdef HAVE_STATUS_BUS
    update_status_bus_state();
#endif
    TypioEngineManager *manager = typio_instance_get_engine_manager(g_instance);
    TypioEngine *active = typio_engine_manager_get_active(manager);
    if (active) {
        typio_log(TYPIO_LOG_INFO, "Engine changed to: %s", typio_engine_get_name(active));
    }
}

int main(int argc, char *argv[]) {
    g_argv = argv;

    static struct option long_options[] = {
        {"config",  required_argument, 0, 'c'},
        {"data",    required_argument, 0, 'd'},
        {"engine-dir", required_argument, 0, 'E'},
        {"engine",  required_argument, 0, 'e'},
        {"list",    no_argument,       0, 'l'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };

    TypioInstanceConfig config = {};
    bool list_only = false;
    bool verbose = false;

    int opt;
    while ((opt = getopt_long(argc, argv, "c:d:E:e:lvhV", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'c':
                config.config_dir = optarg;
                break;
            case 'd':
                config.data_dir = optarg;
                break;
            case 'E':
                config.engine_dir = optarg;
                break;
            case 'e':
                config.default_engine = optarg;
                break;
            case 'l':
                list_only = true;
                break;
            case 'v':
                verbose = true;
                break;
            case 'h':
                print_help(argv[0]);
                return 0;
            case 'V':
                print_version();
                return 0;
            default:
                print_help(argv[0]);
                return 1;
        }
    }

    /* Set up logging */
    config.log_callback = log_callback;
    typio_log_set_level(verbose ? TYPIO_LOG_DEBUG : TYPIO_LOG_INFO);

    /* Create instance */
    g_instance = typio_instance_new_with_config(&config);
    if (!g_instance) {
        fprintf(stderr, "Failed to create Typio instance\n");
        return 1;
    }

    /* Initialize instance */
    TypioResult result = typio_instance_init(g_instance);
    if (result != TYPIO_OK) {
        fprintf(stderr, "Failed to initialize Typio instance: %d\n", result);
        typio_instance_free(g_instance);
        return 1;
    }

    /* List engines and exit if requested */
    if (list_only) {
        list_engines(g_instance);
        typio_instance_free(g_instance);
        return 0;
    }

    /* Set up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Typio Input Method Framework %s started\n", TYPIO_VERSION);
    printf("Configuration: %s\n", typio_instance_get_config_dir(g_instance));
    printf("Data: %s\n", typio_instance_get_data_dir(g_instance));

    /* Print active engine */
    TypioEngineManager *manager = typio_instance_get_engine_manager(g_instance);
    TypioEngine *active = typio_engine_manager_get_active(manager);
    if (active) {
        printf("Active engine: %s\n", typio_engine_get_name(active));
    } else {
        printf("No active engine\n");
    }

    printf("\nPress Ctrl+C to exit\n\n");

    /* Create system tray */
#ifdef HAVE_STATUS_BUS
    g_status_bus = typio_status_bus_new(g_instance);
    if (g_status_bus) {
        printf("D-Bus status interface initialized\n");
    } else {
        printf("D-Bus status interface not available\n");
    }
#endif

#ifdef HAVE_SYSTRAY
    TypioTrayConfig tray_config = {
        .icon_name = "typio-keyboard",
        .tooltip = "Typio Input Method",
        .menu_callback = tray_menu_callback,
        .user_data = nullptr,
    };
    g_tray = typio_tray_new(g_instance, &tray_config);
    if (g_tray && typio_tray_is_registered(g_tray)) {
        update_tray_engine_status();
        printf("System tray initialized\n");
    } else if (g_tray) {
        update_tray_engine_status();
        printf("System tray pending (StatusNotifierWatcher not running yet)\n");
    } else {
        printf("System tray not available (StatusNotifierWatcher may not be running)\n");
    }
#endif

    /* Main event loop */
#ifdef HAVE_WAYLAND
    /* Create Wayland frontend */
    g_wl_frontend = typio_wl_frontend_new(g_instance, nullptr);
    if (!g_wl_frontend) {
        fprintf(stderr, "Failed to create Wayland frontend\n");
        fprintf(stderr, "Make sure the session provides zwp_input_method_manager_v2 and a working text-input-v3 path\n");
#ifdef HAVE_SYSTRAY
        if (g_tray) {
            typio_tray_destroy(g_tray);
            g_tray = nullptr;
        }
#endif
#ifdef HAVE_STATUS_BUS
        if (g_status_bus) {
            typio_status_bus_destroy(g_status_bus);
            g_status_bus = nullptr;
        }
#endif
        typio_instance_free(g_instance);
        return 1;
    }

#ifdef HAVE_SYSTRAY
    /* Integrate tray with Wayland event loop */
    if (g_tray) {
        typio_wl_frontend_set_tray(g_wl_frontend, g_tray);
    }
#endif
#ifdef HAVE_STATUS_BUS
    if (g_status_bus) {
        typio_wl_frontend_set_status_bus(g_wl_frontend, g_status_bus);
    }
#endif

    /* Set engine change callback for external state updates */
    typio_instance_set_engine_changed_callback(g_instance, on_engine_change, nullptr);
    typio_instance_set_status_icon_changed_callback(g_instance, on_status_icon_change, nullptr);

    printf("Wayland input method frontend started\n");

    /* Run the Wayland event loop */
    int wl_result = typio_wl_frontend_run(g_wl_frontend);

    const char *wl_error = typio_wl_frontend_get_error(g_wl_frontend);
    if (wl_error) {
        fprintf(stderr, "Wayland error: %s\n", wl_error);
    }

    typio_wl_frontend_destroy(g_wl_frontend);
    g_wl_frontend = nullptr;

    if (wl_result < 0) {
#ifdef HAVE_SYSTRAY
        if (g_tray) {
            typio_tray_destroy(g_tray);
            g_tray = nullptr;
        }
#endif
#ifdef HAVE_STATUS_BUS
        if (g_status_bus) {
            typio_status_bus_destroy(g_status_bus);
            g_status_bus = nullptr;
        }
#endif
        typio_instance_free(g_instance);
        return 1;
    }
#else
    fprintf(stderr, "This build does not include the Wayland frontend.\n");
    fprintf(stderr, "Reconfigure with ENABLE_WAYLAND=ON to run Typio.\n");
#ifdef HAVE_SYSTRAY
    if (g_tray) {
        typio_tray_destroy(g_tray);
        g_tray = nullptr;
    }
#endif
#ifdef HAVE_STATUS_BUS
    if (g_status_bus) {
        typio_status_bus_destroy(g_status_bus);
        g_status_bus = nullptr;
    }
#endif
    typio_instance_free(g_instance);
    return 1;
#endif

#ifdef HAVE_SYSTRAY
    /* Clean up tray */
    if (g_tray) {
        typio_tray_destroy(g_tray);
        g_tray = nullptr;
    }
#endif
#ifdef HAVE_STATUS_BUS
    if (g_status_bus) {
        typio_status_bus_destroy(g_status_bus);
        g_status_bus = nullptr;
    }
#endif

    printf("\nShutting down...\n");

    /* Cleanup */
    typio_instance_free(g_instance);

    if (g_restart_requested) {
        printf("Restarting...\n");
        execv(g_argv[0], g_argv);
        /* execv only returns on failure */
        perror("execv");
        return 1;
    }

    printf("Goodbye!\n");

    return 0;
}
