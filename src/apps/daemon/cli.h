#ifndef TYPIO_DAEMON_CLI_H
#define TYPIO_DAEMON_CLI_H

#include "typio/instance.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioDaemonOptions {
    TypioInstanceConfig instance_config;
    bool list_only;
    bool verbose;
} TypioDaemonOptions;

void typio_daemon_options_init(TypioDaemonOptions *options);
int typio_daemon_parse_args(TypioDaemonOptions *options, int argc, char *argv[]);
void typio_daemon_print_help(const char *prog);
void typio_daemon_print_version(void);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_DAEMON_CLI_H */
