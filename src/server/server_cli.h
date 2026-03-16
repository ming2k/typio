#ifndef TYPIO_SERVER_CLI_H
#define TYPIO_SERVER_CLI_H

#include "typio/instance.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioServerOptions {
    TypioInstanceConfig instance_config;
    bool list_only;
    bool verbose;
} TypioServerOptions;

void typio_server_options_init(TypioServerOptions *options);
int typio_server_parse_args(TypioServerOptions *options, int argc, char *argv[]);
void typio_server_print_help(const char *prog);
void typio_server_print_version(void);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_SERVER_CLI_H */
