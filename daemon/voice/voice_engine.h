/**
 * @file voice_engine.h
 * @brief Voice engine adapters - exports engine factory functions
 *
 * Each voice engine (whisper, sherpa-onnx) is registered as a
 * TYPIO_ENGINE_TYPE_VOICE engine in engine_manager and exposes a
 * TypioVoiceEngineOps vtable.  The audio infrastructure (PipeWire,
 * threading, state machine) stays in voice_service; the engine adapter
 * owns model loading and inference.
 */

#ifndef TYPIO_VOICE_ENGINE_H
#define TYPIO_VOICE_ENGINE_H

#include "typio/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

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
