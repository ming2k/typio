/**
 * @file voice_service.c
 * @brief Voice input service - state machine, threading, audio buffering
 *
 * Backend-agnostic: delegates speech-to-text to a TypioVoiceBackend
 * (whisper.cpp, sherpa-onnx, etc.) selected via configuration.
 */

#include "typio_build_config.h"
#include "voice_service.h"
#include "voice_backend.h"
#include "pw_capture.h"
#include "typio/config.h"
#include "typio/instance.h"
#include "typio/input_context.h"
#include "utils/log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define TYPIO_VOICE_INITIAL_BUFFER_SIZE (16000 * 30) /* 30 seconds at 16kHz */

typedef enum {
    TYPIO_VOICE_IDLE = 0,
    TYPIO_VOICE_RECORDING,
    TYPIO_VOICE_PROCESSING,
} TypioVoiceState;

struct TypioVoiceService {
    TypioInstance *instance;
    TypioVoiceBackend *backend;
    TypioPwCapture *capture;

    /* State machine */
    TypioVoiceState state;

    /* Audio buffer (protected by mutex) */
    float *audio_buffer;
    size_t audio_len;
    size_t audio_cap;
    pthread_mutex_t buffer_mutex;

    /* Inference thread */
    pthread_t infer_thread;
    int event_fd;
    char *result;
    pthread_mutex_t result_mutex;
};

static void audio_callback(const float *samples, size_t count, void *user_data) {
    TypioVoiceService *svc = user_data;

    pthread_mutex_lock(&svc->buffer_mutex);

    if (svc->state != TYPIO_VOICE_RECORDING) {
        pthread_mutex_unlock(&svc->buffer_mutex);
        return;
    }

    /* Grow buffer if needed */
    size_t needed = svc->audio_len + count;
    if (needed > svc->audio_cap) {
        size_t new_cap = svc->audio_cap * 2;
        if (new_cap < needed) {
            new_cap = needed;
        }
        float *new_buf = realloc(svc->audio_buffer, new_cap * sizeof(float));
        if (!new_buf) {
            pthread_mutex_unlock(&svc->buffer_mutex);
            return;
        }
        svc->audio_buffer = new_buf;
        svc->audio_cap = new_cap;
    }

    memcpy(svc->audio_buffer + svc->audio_len, samples,
           count * sizeof(float));
    svc->audio_len += count;

    pthread_mutex_unlock(&svc->buffer_mutex);
}

static void *inference_thread(void *arg) {
    TypioVoiceService *svc = arg;

    /* Take ownership of the audio data */
    pthread_mutex_lock(&svc->buffer_mutex);
    float *audio = svc->audio_buffer;
    size_t audio_len = svc->audio_len;
    svc->audio_buffer = nullptr;
    svc->audio_len = 0;
    svc->audio_cap = 0;
    pthread_mutex_unlock(&svc->buffer_mutex);

    char *result_text = nullptr;

    if (audio && audio_len > 0) {
        result_text = typio_voice_backend_process(svc->backend,
                                                   audio, audio_len);
    }

    free(audio);

    /* Store result and notify main thread */
    pthread_mutex_lock(&svc->result_mutex);
    free(svc->result);
    svc->result = result_text;
    pthread_mutex_unlock(&svc->result_mutex);

    uint64_t val = 1;
    if (write(svc->event_fd, &val, sizeof(val)) < 0) {
        typio_log(TYPIO_LOG_ERROR, "Failed to signal eventfd");
    }

    return nullptr;
}

/**
 * Try to create a voice backend based on configuration.
 * Falls back through available backends if the configured one fails.
 */
static TypioVoiceBackend *create_backend(TypioConfig *config,
                                          const char *data_dir) {
    const char *backend_name = "whisper";
    const char *language = nullptr; /* NULL = auto-detect */
    const char *model = nullptr;

    if (config) {
        /* New unified [voice] section takes priority */
        backend_name = typio_config_get_string(config, "voice.backend",
                                                nullptr);
        if (backend_name) {
            language = typio_config_get_string(config, "voice.language",
                                               nullptr);
            model = typio_config_get_string(config, "voice.model", nullptr);
        } else {
            /* Fall back to legacy [whisper] section */
            backend_name = "whisper";
            language = typio_config_get_string(config, "whisper.language",
                                               nullptr);
            model = typio_config_get_string(config, "whisper.model", "base");
        }
    }

    typio_log(TYPIO_LOG_INFO,
              "Voice backend: trying '%s' (lang=%s, model=%s)",
              backend_name, language, model ? model : "(default)");

    TypioVoiceBackend *backend = nullptr;

    if (strcmp(backend_name, "sherpa-onnx") == 0 ||
        strcmp(backend_name, "sherpa") == 0) {
#ifdef HAVE_SHERPA_ONNX
        backend = typio_voice_backend_sherpa_new(data_dir, language, model);
#else
        typio_log(TYPIO_LOG_WARNING,
                  "sherpa-onnx backend requested but not built "
                  "(rebuild with -DBUILD_SHERPA_ONNX=ON)");
#endif
    } else if (strcmp(backend_name, "whisper") == 0) {
#ifdef HAVE_WHISPER
        backend = typio_voice_backend_whisper_new(data_dir, language,
                                                   model ? model : "base");
#else
        typio_log(TYPIO_LOG_WARNING,
                  "whisper backend requested but not built "
                  "(rebuild with -DBUILD_WHISPER=ON)");
#endif
    } else {
        typio_log(TYPIO_LOG_WARNING,
                  "Unknown voice backend '%s'", backend_name);
    }

    if (backend) {
        typio_log(TYPIO_LOG_INFO, "Voice backend '%s' ready", backend_name);
    }

    return backend;
}

TypioVoiceService *typio_voice_service_new(TypioInstance *instance) {
    if (!instance) {
        return nullptr;
    }

    TypioVoiceService *svc = calloc(1, sizeof(TypioVoiceService));
    if (!svc) {
        return nullptr;
    }

    svc->instance = instance;
    svc->state = TYPIO_VOICE_IDLE;
    svc->event_fd = -1;

    pthread_mutex_init(&svc->buffer_mutex, nullptr);
    pthread_mutex_init(&svc->result_mutex, nullptr);

    /* Create backend */
    TypioConfig *config = typio_instance_get_config(instance);
    const char *data_dir = typio_instance_get_data_dir(instance);

    svc->backend = create_backend(config, data_dir);
    if (!svc->backend) {
        typio_log(TYPIO_LOG_WARNING,
                  "No voice backend available (voice input disabled)");
        return svc;
    }

    /* Create eventfd for thread notification */
    svc->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (svc->event_fd < 0) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create eventfd");
        typio_voice_backend_destroy(svc->backend);
        svc->backend = nullptr;
        pthread_mutex_destroy(&svc->buffer_mutex);
        pthread_mutex_destroy(&svc->result_mutex);
        free(svc);
        return nullptr;
    }

    /* Create PipeWire capture */
    svc->capture = typio_pw_capture_new(audio_callback, svc);
    if (!svc->capture) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create PipeWire capture");
        close(svc->event_fd);
        typio_voice_backend_destroy(svc->backend);
        svc->backend = nullptr;
        pthread_mutex_destroy(&svc->buffer_mutex);
        pthread_mutex_destroy(&svc->result_mutex);
        free(svc);
        return nullptr;
    }

    typio_log(TYPIO_LOG_INFO, "Voice service initialized");
    return svc;
}

void typio_voice_service_free(TypioVoiceService *svc) {
    if (!svc) {
        return;
    }

    /* Stop any ongoing recording */
    if (svc->state == TYPIO_VOICE_RECORDING) {
        typio_pw_capture_stop(svc->capture);
    }

    /* Wait for inference thread if running */
    if (svc->state == TYPIO_VOICE_PROCESSING) {
        pthread_join(svc->infer_thread, nullptr);
    }

    if (svc->capture) {
        typio_pw_capture_free(svc->capture);
    }

    if (svc->event_fd >= 0) {
        close(svc->event_fd);
    }

    typio_voice_backend_destroy(svc->backend);

    free(svc->audio_buffer);
    free(svc->result);
    pthread_mutex_destroy(&svc->buffer_mutex);
    pthread_mutex_destroy(&svc->result_mutex);
    free(svc);
}

bool typio_voice_service_start(TypioVoiceService *svc) {
    if (!svc || !svc->backend || svc->state != TYPIO_VOICE_IDLE) {
        return false;
    }

    /* Allocate fresh audio buffer */
    pthread_mutex_lock(&svc->buffer_mutex);
    free(svc->audio_buffer);
    svc->audio_buffer = calloc(TYPIO_VOICE_INITIAL_BUFFER_SIZE, sizeof(float));
    svc->audio_len = 0;
    svc->audio_cap = TYPIO_VOICE_INITIAL_BUFFER_SIZE;
    pthread_mutex_unlock(&svc->buffer_mutex);

    if (!svc->audio_buffer) {
        return false;
    }

    if (!typio_pw_capture_start(svc->capture)) {
        free(svc->audio_buffer);
        svc->audio_buffer = nullptr;
        return false;
    }

    svc->state = TYPIO_VOICE_RECORDING;
    typio_log(TYPIO_LOG_INFO, "Voice recording started");
    return true;
}

void typio_voice_service_stop(TypioVoiceService *svc) {
    if (!svc || svc->state != TYPIO_VOICE_RECORDING) {
        return;
    }

    typio_pw_capture_stop(svc->capture);

    svc->state = TYPIO_VOICE_PROCESSING;
    typio_log(TYPIO_LOG_INFO, "Voice recording stopped, starting inference "
              "(%zu samples)", svc->audio_len);

    /* Launch inference thread */
    if (pthread_create(&svc->infer_thread, nullptr, inference_thread, svc) != 0) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create inference thread");
        svc->state = TYPIO_VOICE_IDLE;
        pthread_mutex_lock(&svc->buffer_mutex);
        free(svc->audio_buffer);
        svc->audio_buffer = nullptr;
        svc->audio_len = 0;
        svc->audio_cap = 0;
        pthread_mutex_unlock(&svc->buffer_mutex);
        return;
    }
}

int typio_voice_service_get_fd(TypioVoiceService *svc) {
    if (!svc || svc->event_fd < 0) {
        return -1;
    }
    return svc->event_fd;
}

void typio_voice_service_dispatch(TypioVoiceService *svc,
                                   TypioInputContext *ctx) {
    if (!svc) {
        return;
    }

    /* Read and clear eventfd */
    uint64_t val;
    if (read(svc->event_fd, &val, sizeof(val)) < 0) {
        return;
    }

    /* Join the inference thread */
    pthread_join(svc->infer_thread, nullptr);

    /* Retrieve result */
    pthread_mutex_lock(&svc->result_mutex);
    char *text = svc->result;
    svc->result = nullptr;
    pthread_mutex_unlock(&svc->result_mutex);

    svc->state = TYPIO_VOICE_IDLE;

    if (text && text[0] != '\0' && ctx) {
        /* Trim leading whitespace (some backends add a leading space) */
        const char *p = text;
        while (*p == ' ') {
            p++;
        }
        if (*p != '\0') {
            typio_log(TYPIO_LOG_INFO, "Voice result: \"%s\"", p);
            typio_input_context_commit(ctx, p);
        }
    }

    free(text);
}

bool typio_voice_service_is_available(TypioVoiceService *svc) {
    return svc && svc->backend && svc->capture;
}

void typio_voice_service_reload(TypioVoiceService *svc) {
    if (!svc || !svc->instance) {
        return;
    }

    /* Don't reload while recording or processing */
    if (svc->state != TYPIO_VOICE_IDLE) {
        typio_log(TYPIO_LOG_INFO,
                  "Voice reload deferred: service busy (state=%d)", svc->state);
        return;
    }

    TypioConfig *config = typio_instance_get_config(svc->instance);
    const char *data_dir = typio_instance_get_data_dir(svc->instance);

    TypioVoiceBackend *new_backend = create_backend(config, data_dir);

    /* Swap backend */
    if (svc->backend) {
        typio_voice_backend_destroy(svc->backend);
    }
    svc->backend = new_backend;

    if (new_backend) {
        /* Ensure capture and eventfd exist (may not if initial init had no backend) */
        if (!svc->capture) {
            svc->capture = typio_pw_capture_new(audio_callback, svc);
        }
        if (svc->event_fd < 0) {
            svc->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        }
        typio_log(TYPIO_LOG_INFO, "Voice service reloaded: backend ready");
    } else {
        typio_log(TYPIO_LOG_WARNING,
                  "Voice service reloaded: no backend available");
    }
}
