/**
 * @file voice.h
 * @brief Public voice input API
 *
 * Core owns the voice session lifecycle, state machine, and engine management.
 * Frontends (daemon) inject a platform-specific audio source and register an
 * event callback to receive results and state changes.
 */

#ifndef TYPIO_VOICE_H
#define TYPIO_VOICE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque handles ────────────────────────────────────────────────────── */

typedef struct TypioVoiceSession TypioVoiceSession;

/* ── Audio source abstraction (provided by frontend) ───────────────────── */

typedef struct TypioAudioSource TypioAudioSource;

typedef struct {
    bool (*start)(TypioAudioSource *source);
    void (*stop)(TypioAudioSource *source);
    void (*free)(TypioAudioSource *source);
    int  (*get_fd)(TypioAudioSource *source);
    void (*dispatch)(TypioAudioSource *source);
} TypioAudioSourceOps;

struct TypioAudioSource {
    const TypioAudioSourceOps *ops;
};

/* ── Voice session events ──────────────────────────────────────────────── */

typedef enum {
    TYPIO_VOICE_STATE_IDLE = 0,
    TYPIO_VOICE_STATE_LOADING,
    TYPIO_VOICE_STATE_RECORDING,
    TYPIO_VOICE_STATE_PROCESSING,
} TypioVoiceState;

typedef enum {
    TYPIO_VOICE_EVENT_STATE_CHANGE,
    TYPIO_VOICE_EVENT_RESULT,
    TYPIO_VOICE_EVENT_ERROR,
} TypioVoiceSessionEventType;

typedef struct {
    TypioVoiceSessionEventType type;
    TypioVoiceState     state;
    char               *text;   /**< RESULT: heap-allocated, caller must free */
    const char         *error;  /**< ERROR: borrowed, do not free */
} TypioVoiceSessionEvent;

typedef void (*TypioVoiceSessionEventCallback)(const TypioVoiceSessionEvent *event,
                                        void *user_data);

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

TypioVoiceSession *typio_voice_session_new(TypioInstance *instance);
void typio_voice_session_free(TypioVoiceSession *session);

/* ── Audio source injection (must be set before start) ─────────────────── */

void typio_voice_session_set_audio_source(TypioVoiceSession *session,
                                          TypioAudioSource *source);

/** Push audio samples into the session (called by the audio source callback). */
void typio_voice_session_feed_audio(TypioVoiceSession *session,
                                    const float *samples, size_t count);

/* ── Event callback ────────────────────────────────────────────────────── */

void typio_voice_session_set_callback(TypioVoiceSession *session,
                                      TypioVoiceSessionEventCallback callback,
                                      void *user_data);

/* ── Control ───────────────────────────────────────────────────────────── */

bool typio_voice_session_start(TypioVoiceSession *session);
void typio_voice_session_stop(TypioVoiceSession *session);
bool typio_voice_session_is_available(const TypioVoiceSession *session);
const char *typio_voice_session_get_unavail_reason(const TypioVoiceSession *session);

/* ── Event-loop integration (fd-based dispatch) ────────────────────────── */

int  typio_voice_session_get_fd(TypioVoiceSession *session);
void typio_voice_session_dispatch(TypioVoiceSession *session);

/* ── Idle timeout ──────────────────────────────────────────────────────── */

void typio_voice_session_set_idle_timeout_ms(TypioVoiceSession *session,
                                             uint32_t timeout_ms);

/* ── Engine reload ─────────────────────────────────────────────────────── */

void typio_voice_session_reload_engine(TypioVoiceSession *session);

/* ── Utility ───────────────────────────────────────────────────────────── */

void typio_voice_filter_tags_inplace(char *text);

/* ── Engine factories (for engine_manager registration) ────────────────── */

#ifdef HAVE_WHISPER
extern TypioEngine *typio_engine_create_whisper(void);
extern const TypioEngineInfo *typio_engine_get_info_whisper(void);
#endif

#ifdef HAVE_SHERPA_ONNX
extern TypioEngine *typio_engine_create_sherpa(void);
extern const TypioEngineInfo *typio_engine_get_info_sherpa(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_VOICE_H */
