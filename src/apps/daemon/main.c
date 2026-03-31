#include "app.h"
#include "cli.h"

int main(int argc, char *argv[]) {
    TypioDaemonOptions options;
    TypioDaemonApp app;
    int parse_result;
    int exit_code;

    typio_daemon_options_init(&options);
    parse_result = typio_daemon_parse_args(&options, argc, argv);
    if (parse_result >= 0) {
        return parse_result;
    }

    if (!typio_daemon_app_init(&app, &options.instance_config, options.verbose, argv)) {
        return 1;
    }

    if (options.list_only) {
        typio_daemon_app_list_engines(&app);
        typio_daemon_app_shutdown(&app);
        return 0;
    }

    exit_code = typio_daemon_app_run(&app);
    typio_daemon_app_shutdown(&app);
    return typio_daemon_app_finish(&app, exit_code);
}
