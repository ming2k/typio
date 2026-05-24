/**
 * @file tip_protocol.c
 * @brief Socket path helper.
 */

#include "tip_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *typio_ipc_socket_path(void)
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *home = getenv("HOME");
    char *path;
    int len;

    if (runtime_dir && *runtime_dir) {
        len = snprintf(NULL, 0, "%s/typio/daemon.sock", runtime_dir);
        path = malloc((size_t)len + 1);
        if (path)
            snprintf(path, (size_t)len + 1, "%s/typio/daemon.sock", runtime_dir);
        return path;
    }

    if (home && *home) {
        len = snprintf(NULL, 0, "%s/.local/share/typio/daemon.sock", home);
        path = malloc((size_t)len + 1);
        if (path)
            snprintf(path, (size_t)len + 1, "%s/.local/share/typio/daemon.sock", home);
        return path;
    }

    /* Last resort: /tmp */
    path = strdup("/tmp/typio-daemon.sock");
    return path;
}
