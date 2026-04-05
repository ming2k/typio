#ifndef TYPIO_DAEMON_APP_H
#define TYPIO_DAEMON_APP_H

#include "typio_build_config.h"
#include "typio/instance.h"

#include <signal.h>

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

typedef struct TypioDaemonApp {
    TypioInstance *instance;
    char **argv;
    bool restart_requested;
    bool shutdown_requested_by_signal;
    volatile sig_atomic_t shutdown_signal;
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
} TypioDaemonApp;

bool typio_daemon_app_init(TypioDaemonApp *app,
                           const TypioInstanceConfig *config,
                           bool verbose,
                           char *argv[]);
void typio_daemon_app_list_engines(TypioDaemonApp *app);
int typio_daemon_app_run(TypioDaemonApp *app);
void typio_daemon_app_shutdown(TypioDaemonApp *app);
int typio_daemon_app_finish(TypioDaemonApp *app, int exit_code);

#ifdef TYPIO_DAEMON_TEST
void typio_daemon_test_update_tray_engine_status(TypioDaemonApp *app);
void typio_daemon_test_on_engine_change(TypioInstance *instance,
                                        const TypioEngineInfo *engine,
                                        void *user_data);
void typio_daemon_test_on_voice_engine_change(TypioInstance *instance,
                                              const TypioEngineInfo *engine,
                                              void *user_data);
void typio_daemon_test_on_status_icon_change(TypioInstance *instance,
                                             const char *icon_name,
                                             void *user_data);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_DAEMON_APP_H */
