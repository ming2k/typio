#include "server_app.h"
#include "server_cli.h"

int main(int argc, char *argv[]) {
    TypioServerOptions options;
    TypioServerApp app;
    int parse_result;
    int exit_code;

    typio_server_options_init(&options);
    parse_result = typio_server_parse_args(&options, argc, argv);
    if (parse_result >= 0) {
        return parse_result;
    }

    if (!typio_server_app_init(&app, &options.instance_config, options.verbose, argv)) {
        return 1;
    }

    if (options.list_only) {
        typio_server_app_list_engines(&app);
        typio_server_app_shutdown(&app);
        return 0;
    }

    exit_code = typio_server_app_run(&app);
    typio_server_app_shutdown(&app);
    return typio_server_app_finish(&app, exit_code);
}
