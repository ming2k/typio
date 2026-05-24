/**
 * @file voice_service.c
 * @brief Voice input service - state machine, threading, audio buffering
 *
 * Backend-agnostic: delegates speech-to-text to the active voice engine's
 * TypioVoiceEngineOps::process_audio callback obtained from engine_manager.
 *
 * Lifecycle contract
 * ------------------
 * The inference thread snapshots a borrowed @c voice_engine pointer under
 * @c buffer_mutex and then calls @c engine->voice->process_audio outside the
 * lock for what may be several seconds. Two invariants make that safe:
 *
 *   1. The @c TypioEngine struct must outlive the voice service.
 *      Enforced by daemon teardown order: @c wl_frontend.c frees the voice
 *      service before unbinding the engine manager. Any future caller that
 *      tears the daemon down out of order must respect this — the assertion
 *      in @c typio_voice_service_free flags violations.
 *
 *   2. The underlying speech-to-text BACKEND owned by that engine is
 *      reference-counted via @c voice_proxy. So while the engine pointer
 *      stays valid by virtue of (1), the backend the inference is touching
 *      stays valid by virtue of the proxy's pending-destroy parking. That
 *      means @c engine_manager_unload, @c set_active_voice, and engine
 *      @c deactivate may all run concurrently with in-flight inference;
 *      the proxy handles deferred release.
 *
 * In other words: voice_service relies on the proxy for backend safety, and
 * on teardown order for engine safety. Don't add a third dependency without
 * extending the contract here.
 */

#include "typio_build_config.h"
#include "voice_service.h"
#include "voice_engine.h"
#include "pw_capture.h"
#include "typio/config.h"
#include "typio/instance.h"
#include "typio/engine_manager.h"
#include "typio/input_context.h"
#include "typio/log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define TYPIO_VOICE_INITIAL_BUFFER_SIZE (16000 * 30) /* 30 seconds at 16kHz */
#define TYPIO_VOICE_SAMPLE_RATE 16000
#define TYPIO_VOICE_TRIM_THRESHOLD 0.003f
#define TYPIO_VOICE_TRIM_PADDING_SAMPLES (TYPIO_VOICE_SAMPLE_RATE / 10)
#define TYPIO_VOICE_MIN_ACTIVE_SAMPLES (TYPIO_VOICE_SAMPLE_RATE / 5)

static bool engine_has_voice(TypioEngine *engine) {
    if (!engine || !engine->voice || !engine->voice->process_audio)
        return false;
    if (engine->voice->is_ready)
        return engine->voice->is_ready(engine);
    return true;
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

static float sample_abs(float v) {
    return v < 0.0f ? -v : v;
}

static size_t prepare_audio_for_inference(float *audio, size_t audio_len) {
    float peak = 0.0f;
    double abs_sum = 0.0;
    size_t first_active = audio_len;
    size_t last_active = 0;

    if (!audio || audio_len == 0) {
        return 0;
    }

    for (size_t i = 0; i < audio_len; i++) {
        float amp = sample_abs(audio[i]);
        if (amp > peak) {
            peak = amp;
        }
        abs_sum += amp;
        if (amp >= TYPIO_VOICE_TRIM_THRESHOLD) {
            if (first_active == audio_len) {
                first_active = i;
            }
            last_active = i;
        }
    }

    typio_log(TYPIO_LOG_INFO,
              "Voice audio level: duration=%.2fs peak=%.5f mean_abs=%.5f",
              (double)audio_len / (double)TYPIO_VOICE_SAMPLE_RATE,
              (double)peak,
              abs_sum / (double)audio_len);

    if (first_active == audio_len ||
        last_active <= first_active ||
        last_active - first_active + 1 < TYPIO_VOICE_MIN_ACTIVE_SAMPLES) {
        typio_log(TYPIO_LOG_WARNING,
                  "Voice audio discarded: no usable microphone signal detected");
        return 0;
    }

    size_t start = first_active > TYPIO_VOICE_TRIM_PADDING_SAMPLES
        ? first_active - TYPIO_VOICE_TRIM_PADDING_SAMPLES
        : 0;
    size_t end = last_active + TYPIO_VOICE_TRIM_PADDING_SAMPLES + 1;
    if (end > audio_len) {
        end = audio_len;
    }

    if (start > 0 || end < audio_len) {
        size_t trimmed_len = end - start;
        memmove(audio, audio + start, trimmed_len * sizeof(float));
        typio_log(TYPIO_LOG_INFO,
                  "Voice audio trimmed: %.2fs -> %.2fs",
                  (double)audio_len / (double)TYPIO_VOICE_SAMPLE_RATE,
                  (double)trimmed_len / (double)TYPIO_VOICE_SAMPLE_RATE);
        return trimmed_len;
    }

    return audio_len;
}

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
    size_t raw_audio_len = audio_len;
    audio_len = prepare_audio_for_inference(audio, audio_len);

    if (!audio || audio_len == 0) {
        typio_log(TYPIO_LOG_WARNING,
                  "Voice inference: no audio captured or usable (%zu raw samples)",
                  raw_audio_len);
    } else if (!engine || !engine->voice || !engine->voice->process_audio) {
        typio_log(TYPIO_LOG_WARNING, "Voice inference: no engine available");
    } else if (engine->voice->is_ready && !engine->voice->is_ready(engine)) {
        typio_log(TYPIO_LOG_WARNING, "Voice inference: model not loaded");
    } else {
        typio_log(TYPIO_LOG_INFO, "Voice inference: processing %zu samples", audio_len);
        result_text = engine->voice->process_audio(engine, audio, audio_len);
        if (result_text) {
            typio_log(TYPIO_LOG_INFO, "Voice inference: got result (%zu chars)",
                      strlen(result_text));
        } else {
            typio_log(TYPIO_LOG_WARNING, "Voice inference: engine returned NULL");
        }
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

    /* Wait for inference thread if running. After this join returns no
     * thread holds the borrowed engine pointer, so the caller may proceed
     * to tear down the engine manager. Invariant (1) in the file header. */
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

/** Remove [...] tags (e.g. [inaudible], [music]) from voice text in-place. */
void typio_voice_filter_tags_inplace(char *text) {
    if (!text || !text[0]) return;

    char *write = text;
    const char *read = text;

    while (*read) {
        if (*read == '[') {
            const char *tag_end = read + 1;
            while (*tag_end && *tag_end != ']') tag_end++;
            if (*tag_end == ']') {
                read = tag_end + 1;
                while (*read == ' ') read++;
                if (write > text && *read && *(write - 1) != ' ') {
                    *write++ = ' ';
                }
                continue;
            }
        }
        *write++ = *read++;
    }
    *write = '\0';
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
        typio_log(TYPIO_LOG_INFO, "Voice raw: \"%s\"", text);
        typio_voice_filter_tags_inplace(text);

        /* Trim leading whitespace (some backends add a leading space) */
        const char *p = text;
        while (*p == ' ') {
            p++;
        }
        if (*p != '\0') {
            typio_log(TYPIO_LOG_INFO, "Voice result: \"%s\"", p);
            typio_input_context_commit(ctx, p);
        } else {
            typio_log(TYPIO_LOG_INFO, "Voice result: (empty after tag filter)");
        }
    }

    free(text);
}

char *typio_voice_service_collect(TypioVoiceService *svc) {
    bool reload_pending;

    if (!svc) return nullptr;

    uint64_t val;
    if (read(svc->event_fd, &val, sizeof(val)) < 0)
        return nullptr;

    pthread_join(svc->infer_thread, nullptr);

    pthread_mutex_lock(&svc->result_mutex);
    char *text = svc->result;
    svc->result = nullptr;
    pthread_mutex_unlock(&svc->result_mutex);

    pthread_mutex_lock(&svc->buffer_mutex);
    svc->state = TYPIO_VOICE_IDLE;
    reload_pending = svc->reload_pending;
    pthread_mutex_unlock(&svc->buffer_mutex);

    if (reload_pending)
        voice_service_reload_idle(svc);

    return text;
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

    if (!voice_engine)
        return "no voice engine active";
    if (!voice_engine->voice || !voice_engine->voice->process_audio)
        return "voice engine missing process_audio";
    if (voice_engine->voice->is_ready && !voice_engine->voice->is_ready(voice_engine))
        return "voice model not loaded";
    if (!has_capture)
        return "audio capture unavailable";
    return nullptr;
}
