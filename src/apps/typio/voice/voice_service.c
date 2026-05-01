/**
 * @file voice_service.c
 * @brief Voice input service - state machine, threading, audio buffering
 *
 * Backend-agnostic: delegates speech-to-text to the active voice engine's
 * TypioVoiceEngineOps::process_audio callback obtained from engine_manager.
 */

#include "typio_build_config.h"
#include "voice_service.h"
#include "voice_engine.h"
#include "pw_capture.h"
#include "typio/config.h"
#include "typio/instance.h"
#include "typio/engine_manager.h"
#include "typio/input_context.h"
#include "utils/log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define TYPIO_VOICE_INITIAL_BUFFER_SIZE (16000 * 30) /* 30 seconds at 16kHz */

static bool engine_has_voice(TypioEngine *engine) {
    return engine && engine->voice && engine->voice->process_audio;
}

typedef enum {
    TYPIO_VOICE_IDLE = 0,
    TYPIO_VOICE_RECORDING,
    TYPIO_VOICE_PROCESSING,
} TypioVoiceState;

struct TypioVoiceService {
    TypioInstance *instance;
    TypioEngine *voice_engine;      /* Borrowed from engine_manager */
    TypioPwCapture *capture;

    /* State machine */
    TypioVoiceState state;
    bool reload_pending;

    /* Audio buffer, state, reload_pending, and voice_engine are protected by mutex. */
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

static void audio_callback(const float *samples, size_t count, void *user_data);

static void voice_service_reload_idle(TypioVoiceService *svc) {
    TypioEngineManager *mgr;
    TypioEngine *new_engine;

    if (!svc || !svc->instance) {
        return;
    }

    mgr = typio_instance_get_engine_manager(svc->instance);
    new_engine = typio_engine_manager_get_active_voice(mgr);

    pthread_mutex_lock(&svc->buffer_mutex);
    svc->voice_engine = new_engine;
    svc->reload_pending = false;
    pthread_mutex_unlock(&svc->buffer_mutex);

    if (engine_has_voice(new_engine) && svc->capture) {
        typio_log(TYPIO_LOG_INFO, "Voice service reloaded: engine ready");
    } else {
        typio_log(TYPIO_LOG_WARNING,
                  "Voice service reloaded: no voice engine available");
    }
}

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

    /* Take ownership of the audio data and snapshot the engine pointer.
     * Both are read under buffer_mutex to establish happens-before with
     * the main thread (which only modifies voice_engine while IDLE,
     * i.e. after joining this thread). */
    pthread_mutex_lock(&svc->buffer_mutex);
    float *audio = svc->audio_buffer;
    size_t audio_len = svc->audio_len;
    TypioEngine *engine = svc->voice_engine;
    svc->audio_buffer = nullptr;
    svc->audio_len = 0;
    svc->audio_cap = 0;
    pthread_mutex_unlock(&svc->buffer_mutex);

    char *result_text = nullptr;

    if (audio && audio_len > 0 && engine_has_voice(engine)) {
        result_text = engine->voice->process_audio(engine, audio, audio_len);
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

    /* Get voice engine from engine_manager */
    TypioEngineManager *mgr = typio_instance_get_engine_manager(instance);
    svc->voice_engine = typio_engine_manager_get_active_voice(mgr);

    if (!engine_has_voice(svc->voice_engine)) {
        typio_log(TYPIO_LOG_WARNING,
                  "No voice engine available (voice input disabled)");
        return svc;
    }

    /* Create eventfd for thread notification */
    svc->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (svc->event_fd < 0) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create eventfd");
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
        pthread_mutex_destroy(&svc->buffer_mutex);
        pthread_mutex_destroy(&svc->result_mutex);
        free(svc);
        return nullptr;
    }

    typio_log(TYPIO_LOG_INFO, "Voice service initialized");
    return svc;
}

void typio_voice_service_free(TypioVoiceService *svc) {
    TypioVoiceState state;

    if (!svc) {
        return;
    }

    pthread_mutex_lock(&svc->buffer_mutex);
    state = svc->state;
    if (svc->state == TYPIO_VOICE_RECORDING) {
        svc->state = TYPIO_VOICE_IDLE;
    }
    pthread_mutex_unlock(&svc->buffer_mutex);

    /* Stop any ongoing recording */
    if (state == TYPIO_VOICE_RECORDING) {
        typio_pw_capture_stop(svc->capture);
    }

    /* Wait for inference thread if running */
    if (state == TYPIO_VOICE_PROCESSING) {
        pthread_join(svc->infer_thread, nullptr);
    }

    if (svc->capture) {
        typio_pw_capture_free(svc->capture);
    }

    if (svc->event_fd >= 0) {
        close(svc->event_fd);
    }

    /* Don't destroy the engine - engine_manager owns them */

    free(svc->audio_buffer);
    free(svc->result);
    pthread_mutex_destroy(&svc->buffer_mutex);
    pthread_mutex_destroy(&svc->result_mutex);
    free(svc);
}

bool typio_voice_service_start(TypioVoiceService *svc) {
    bool allocated = false;

    if (!svc) {
        return false;
    }

    /* Allocate fresh audio buffer */
    pthread_mutex_lock(&svc->buffer_mutex);
    if (!engine_has_voice(svc->voice_engine) || svc->state != TYPIO_VOICE_IDLE || !svc->capture) {
        pthread_mutex_unlock(&svc->buffer_mutex);
        return false;
    }
    free(svc->audio_buffer);
    svc->audio_buffer = calloc(TYPIO_VOICE_INITIAL_BUFFER_SIZE, sizeof(float));
    svc->audio_len = 0;
    svc->audio_cap = TYPIO_VOICE_INITIAL_BUFFER_SIZE;
    if (svc->audio_buffer) {
        svc->state = TYPIO_VOICE_RECORDING;
        allocated = true;
    }
    pthread_mutex_unlock(&svc->buffer_mutex);

    if (!allocated) {
        return false;
    }

    if (!typio_pw_capture_start(svc->capture)) {
        pthread_mutex_lock(&svc->buffer_mutex);
        free(svc->audio_buffer);
        svc->audio_buffer = nullptr;
        svc->audio_len = 0;
        svc->audio_cap = 0;
        svc->state = TYPIO_VOICE_IDLE;
        pthread_mutex_unlock(&svc->buffer_mutex);
        return false;
    }

    typio_log(TYPIO_LOG_INFO, "Voice recording started");
    return true;
}

void typio_voice_service_stop(TypioVoiceService *svc) {
    size_t sample_count;

    if (!svc) {
        return;
    }

    pthread_mutex_lock(&svc->buffer_mutex);
    if (svc->state != TYPIO_VOICE_RECORDING) {
        pthread_mutex_unlock(&svc->buffer_mutex);
        return;
    }
    svc->state = TYPIO_VOICE_PROCESSING;
    sample_count = svc->audio_len;
    pthread_mutex_unlock(&svc->buffer_mutex);

    typio_pw_capture_stop(svc->capture);

    typio_log(TYPIO_LOG_INFO, "Voice recording stopped, starting inference "
              "(%zu samples)", sample_count);

    /* Launch inference thread */
    if (pthread_create(&svc->infer_thread, nullptr, inference_thread, svc) != 0) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create inference thread");
        pthread_mutex_lock(&svc->buffer_mutex);
        svc->state = TYPIO_VOICE_IDLE;
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
    bool reload_pending;

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

    pthread_mutex_lock(&svc->buffer_mutex);
    svc->state = TYPIO_VOICE_IDLE;
    reload_pending = svc->reload_pending;
    pthread_mutex_unlock(&svc->buffer_mutex);

    if (reload_pending) {
        voice_service_reload_idle(svc);
    }

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
    bool available;

    if (!svc) {
        return false;
    }

    pthread_mutex_lock(&svc->buffer_mutex);
    available = engine_has_voice(svc->voice_engine) && svc->capture;
    pthread_mutex_unlock(&svc->buffer_mutex);
    return available;
}

void typio_voice_service_reload(TypioVoiceService *svc) {
    bool busy;

    if (!svc || !svc->instance) {
        return;
    }

    /* Don't reload while recording or processing; remember that the newest
     * runtime selection must be synced once the active voice job finishes. */
    pthread_mutex_lock(&svc->buffer_mutex);
    busy = svc->state != TYPIO_VOICE_IDLE;
    if (busy) {
        svc->reload_pending = true;
    }
    pthread_mutex_unlock(&svc->buffer_mutex);

    if (busy) {
        typio_log(TYPIO_LOG_INFO,
                  "Voice reload deferred: service busy");
        return;
    }

    voice_service_reload_idle(svc);
}

const char *typio_voice_service_get_unavail_reason(TypioVoiceService *svc) {
    TypioEngine *voice_engine;
    bool has_capture;

    if (!svc)
        return "voice service not created";

    pthread_mutex_lock(&svc->buffer_mutex);
    voice_engine = svc->voice_engine;
    has_capture = svc->capture != NULL;
    pthread_mutex_unlock(&svc->buffer_mutex);

    if (engine_has_voice(voice_engine) && has_capture)
        return nullptr;
    if (!voice_engine)
        return "no voice engine active";
    if (!engine_has_voice(voice_engine))
        return "voice engine missing process_audio";
    return "audio capture unavailable";
}
