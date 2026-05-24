/**
 * @file voice_service.h
 * @brief Voice input service (supports whisper.cpp, sherpa-onnx backends)
 */

#ifndef TYPIO_VOICE_SERVICE_H
#define TYPIO_VOICE_SERVICE_H

#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioVoiceService TypioVoiceService;

/* Lifecycle */
TypioVoiceService *typio_voice_service_new(TypioInstance *instance);
void typio_voice_service_free(TypioVoiceService *svc);

/* PTT control (called from key_route) */
bool typio_voice_service_start(TypioVoiceService *svc);
void typio_voice_service_stop(TypioVoiceService *svc);

/* Event loop integration */
int typio_voice_service_get_fd(TypioVoiceService *svc);
void typio_voice_service_dispatch(TypioVoiceService *svc,
                                   TypioInputContext *ctx);
char *typio_voice_service_collect(TypioVoiceService *svc);
bool typio_voice_service_is_available(TypioVoiceService *svc);
void typio_voice_service_reload(TypioVoiceService *svc);

/** Remove [...] tags (e.g. [inaudible], [music]) from voice text in-place. */
void typio_voice_filter_tags_inplace(char *text);

/** Return a human-readable reason why voice is unavailable, or NULL if ready. */
const char *typio_voice_service_get_unavail_reason(TypioVoiceService *svc);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_VOICE_SERVICE_H */
