/**
 * @file voice_service.h
 * @brief Whisper-based voice input service
 */

#ifndef TYPIO_VOICE_SERVICE_H
#define TYPIO_VOICE_SERVICE_H

#include "typio/types.h"
#include <stdbool.h>

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
bool typio_voice_service_is_available(TypioVoiceService *svc);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_VOICE_SERVICE_H */
