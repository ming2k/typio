#ifndef TYPIO_WL_TRACE_H
#define TYPIO_WL_TRACE_H

#include "typio/types.h"

struct TypioWlFrontend;

#ifdef __cplusplus
extern "C" {
#endif

void typio_wl_trace_level(TypioLogLevel level,
                          struct TypioWlFrontend *frontend,
                          const char *topic,
                          const char *format, ...);

void typio_wl_trace(struct TypioWlFrontend *frontend,
                    const char *topic,
                    const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_TRACE_H */
