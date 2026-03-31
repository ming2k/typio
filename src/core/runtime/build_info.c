#include "typio/build_info.h"

#include "typio_build_config.h"

#include <stdio.h>

const char *typio_build_version(void) {
    return TYPIO_VERSION;
}

const char *typio_build_source_label(void) {
    return TYPIO_BUILD_SOURCE_LABEL;
}

const char *typio_build_display_string(void) {
    static char build_display[128];
    static int initialized = 0;

    if (!initialized) {
        snprintf(build_display, sizeof(build_display),
                 "Typio %s (%s)",
                 TYPIO_VERSION,
                 TYPIO_BUILD_SOURCE_LABEL);
        initialized = 1;
    }

    return build_display;
}
