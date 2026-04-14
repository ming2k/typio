#include "app.h"
#include "cli.h"
#include "client_main.h"

#include <string.h>
#include <stdio.h>

static int daemon_main(int argc, char *argv[]) {
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

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "daemon") == 0) {
        /* Shift argv down to hide 'daemon' from daemon_main's parser */
        argv[1] = argv[0];
        return daemon_main(argc - 1, argv + 1);
    }

    /* All other commands go to the client. We pass the whole argc/argv
       because client_main expects argv[1] to be the command (e.g., 'status'). */
    return client_main(argc, argv);
}
