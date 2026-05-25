/**
 * @file voice_backend.h
 * @brief Abstraction layer for speech-to-text backends
 *
 * Each backend (whisper.cpp, sherpa-onnx, etc.) implements these operations.
 * The voice service owns threading, audio capture, and state machine;
 * the backend only handles model loading and inference.
 */

#ifndef TYPIO_VOICE_BACKEND_H
#define TYPIO_VOICE_BACKEND_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioVoiceBackend TypioVoiceBackend;

typedef struct TypioVoiceBackendOps {
    /**
     * Run speech-to-text inference on a buffer of audio samples.
     * @param backend  The backend instance.
     * @param samples  PCM float32 mono 16kHz audio data.
     * @param n_samples Number of samples.
     * @return Heap-allocated result text (caller frees), or NULL on failure.
     */
    char *(*process)(TypioVoiceBackend *backend,
                     const float *samples, size_t n_samples);

    /** Free backend resources. */
    void (*destroy)(TypioVoiceBackend *backend);
} TypioVoiceBackendOps;

struct TypioVoiceBackend {
    const TypioVoiceBackendOps *ops;
    /* Backend implementations extend this struct with their own fields. */
};

/** Convenience: run inference through the backend vtable. */
static inline char *typio_voice_backend_process(TypioVoiceBackend *b,
                                                 const float *samples,
                                                 size_t n_samples) {
    if (b && b->ops && b->ops->process) {
        return b->ops->process(b, samples, n_samples);
    }
    return NULL;
}

/** Convenience: destroy the backend through the vtable. */
static inline void typio_voice_backend_destroy(TypioVoiceBackend *b) {
    if (b && b->ops && b->ops->destroy) {
        b->ops->destroy(b);
    }
}

/*
 * Backend constructors — each returns NULL if the model cannot be loaded.
 *
 * @param data_dir  Typio data directory (e.g. ~/.local/share/typio).
 * @param language  BCP-47 language code (e.g. "zh", "en"), or NULL for auto-detect.
 * @param model     Model name/path hint (backend-specific).
 */

#ifdef HAVE_WHISPER
TypioVoiceBackend *typio_voice_backend_whisper_new(const char *data_dir,
                                                    const char *language,
                                                    const char *model);
#endif

#ifdef HAVE_SHERPA_ONNX
TypioVoiceBackend *typio_voice_backend_sherpa_new(const char *data_dir,
                                                   const char *language,
                                                   const char *model);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_VOICE_BACKEND_H */
