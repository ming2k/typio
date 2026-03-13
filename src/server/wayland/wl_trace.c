/**
 * @file wl_trace.c
 * @brief Consistent Wayland trace logging
 */

#include "wl_trace.h"

#include "wl_frontend_internal.h"
#include "lifecycle.h"
#include "utils/log.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

static void wl_trace_format(TypioLogLevel level,
                            struct TypioWlFrontend *frontend,
                            const char *topic,
                            const char *format,
                            va_list args) {
    char detail[1024];
    char message[1280];
    const char *phase = "no-frontend";
    uint64_t seq = 0;

    if (frontend) {
        frontend->trace_sequence++;
        seq = frontend->trace_sequence;
        phase = typio_wl_lifecycle_phase_name(frontend->lifecycle_phase);
    }

    vsnprintf(detail, sizeof(detail), format, args);

    snprintf(message, sizeof(message),
             "TRACE seq=%" PRIu64 " phase=%s topic=%s %s",
             seq,
             phase,
             topic ? topic : "unknown",
             detail);
    typio_log(level, "%s", message);
}

void typio_wl_trace_level(TypioLogLevel level,
                          struct TypioWlFrontend *frontend,
                          const char *topic,
                          const char *format, ...) {
    va_list args;

    va_start(args, format);
    wl_trace_format(level, frontend, topic, format, args);
    va_end(args);
}

void typio_wl_trace(struct TypioWlFrontend *frontend,
                    const char *topic,
                    const char *format, ...) {
    va_list args;

    va_start(args, format);
    wl_trace_format(TYPIO_LOG_DEBUG, frontend, topic, format, args);
    va_end(args);
}
