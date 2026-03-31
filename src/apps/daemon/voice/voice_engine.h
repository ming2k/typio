/**
 * @file voice_engine.h
 * @brief Voice engine adapters - wraps voice backends as TypioEngine instances
 *
 * Each voice backend (whisper, sherpa-onnx) is registered as a
 * TYPIO_ENGINE_TYPE_VOICE engine in engine_manager.  The audio infrastructure
 * (PipeWire, threading, state machine) stays in voice_service; the engine
 * adapter owns the backend lifetime and configuration.
 */

#ifndef TYPIO_VOICE_ENGINE_H
#define TYPIO_VOICE_ENGINE_H

#include "typio/engine.h"
#include "voice_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Extract the TypioVoiceBackend from a voice engine instance.
 * Returns NULL if engine is NULL or not a voice engine.
 */
TypioVoiceBackend *typio_voice_engine_get_backend(TypioEngine *engine);

#ifdef HAVE_WHISPER
extern const TypioEngineInfo *typio_engine_get_info_whisper(void);
extern TypioEngine *typio_engine_create_whisper(void);
#endif

#ifdef HAVE_SHERPA_ONNX
extern const TypioEngineInfo *typio_engine_get_info_sherpa(void);
extern TypioEngine *typio_engine_create_sherpa(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_VOICE_ENGINE_H */
