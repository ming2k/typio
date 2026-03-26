#ifndef TYPIO_SERVER_APP_H
#define TYPIO_SERVER_APP_H

#include "typio_build_config.h"
#include "typio/instance.h"

#ifdef HAVE_WAYLAND
#include "wayland/wl_frontend.h"
#endif

#ifdef HAVE_STATUS_BUS
#include "status/status.h"
#endif

#ifdef HAVE_SYSTRAY
#include "tray/tray.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioServerApp {
    TypioInstance *instance;
    char **argv;
    bool restart_requested;
    bool shutdown_requested_by_signal;
    char recent_log_dump_path[1024];
#ifdef HAVE_WAYLAND
    TypioWlFrontend *wl_frontend;
#endif
#ifdef HAVE_STATUS_BUS
    TypioStatusBus *status_bus;
#endif
#ifdef HAVE_SYSTRAY
    TypioTray *tray;
#endif
} TypioServerApp;

bool typio_server_app_init(TypioServerApp *app,
                           const TypioInstanceConfig *config,
                           bool verbose,
                           char *argv[]);
void typio_server_app_list_engines(TypioServerApp *app);
int typio_server_app_run(TypioServerApp *app);
void typio_server_app_shutdown(TypioServerApp *app);
int typio_server_app_finish(TypioServerApp *app, int exit_code);

#ifdef TYPIO_SERVER_TEST
void typio_server_test_update_tray_engine_status(TypioServerApp *app);
void typio_server_test_on_engine_change(TypioInstance *instance,
                                        const TypioEngineInfo *engine,
                                        void *user_data);
void typio_server_test_on_voice_engine_change(TypioInstance *instance,
                                              const TypioEngineInfo *engine,
                                              void *user_data);
void typio_server_test_on_status_icon_change(TypioInstance *instance,
                                             const char *icon_name,
                                             void *user_data);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_SERVER_APP_H */
