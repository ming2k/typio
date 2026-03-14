/**
 * @file voice_service.c
 * @brief Whisper voice input service - state machine, threading, model management
 */

#include "voice_service.h"
#include "pw_capture.h"
#include "typio/config.h"
#include "typio/instance.h"
#include "typio/input_context.h"
#include "utils/log.h"

#include <whisper.h>
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
    struct whisper_context *whisper_ctx;
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

    /* Config */
    char language[16];
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
        struct whisper_full_params params =
            whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        params.print_progress = false;
        params.print_special = false;
        params.print_realtime = false;
        params.print_timestamps = false;
        params.single_segment = true;
        params.no_timestamps = true;
        params.language = svc->language;

        int ret = whisper_full(svc->whisper_ctx, params, audio,
                               (int)audio_len);
        if (ret == 0) {
            int n_segments = whisper_full_n_segments(svc->whisper_ctx);
            /* Collect all segment text */
            size_t total_len = 0;
            for (int i = 0; i < n_segments; i++) {
                const char *seg =
                    whisper_full_get_segment_text(svc->whisper_ctx, i);
                if (seg) {
                    total_len += strlen(seg);
                }
            }

            if (total_len > 0) {
                result_text = calloc(total_len + 1, sizeof(char));
                if (result_text) {
                    for (int i = 0; i < n_segments; i++) {
                        const char *seg =
                            whisper_full_get_segment_text(svc->whisper_ctx, i);
                        if (seg) {
                            strcat(result_text, seg);
                        }
                    }
                }
            }
        } else {
            typio_log(TYPIO_LOG_ERROR, "Whisper inference failed: %d", ret);
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
    strncpy(svc->language, "zh", sizeof(svc->language) - 1);

    pthread_mutex_init(&svc->buffer_mutex, nullptr);
    pthread_mutex_init(&svc->result_mutex, nullptr);

    /* Read config */
    const char *config_dir = typio_instance_get_config_dir(instance);
    if (config_dir) {
        char config_path[512];
        snprintf(config_path, sizeof(config_path), "%s/typio.conf", config_dir);
        TypioConfig *config = typio_config_load_file(config_path);
        if (config) {
            const char *lang = typio_config_get_string(config,
                                                        "whisper.language",
                                                        "zh");
            strncpy(svc->language, lang, sizeof(svc->language) - 1);
            svc->language[sizeof(svc->language) - 1] = '\0';

            const char *model_name = typio_config_get_string(config,
                                                              "whisper.model",
                                                              "base");

            /* Build model path before freeing config (model_name points
             * into config's internal storage). */
            const char *data_dir = typio_instance_get_data_dir(instance);
            char model_path[512] = {};
            if (data_dir) {
                snprintf(model_path, sizeof(model_path),
                         "%s/whisper/ggml-%s.bin", data_dir, model_name);
            }
            typio_config_free(config);

            if (model_path[0] != '\0') {

                struct whisper_context_params cparams =
                    whisper_context_default_params();
                svc->whisper_ctx =
                    whisper_init_from_file_with_params(model_path, cparams);
                if (!svc->whisper_ctx) {
                    typio_log(TYPIO_LOG_WARNING,
                              "Whisper model not found: %s "
                              "(voice input disabled)", model_path);
                } else {
                    typio_log(TYPIO_LOG_INFO,
                              "Whisper model loaded: %s", model_path);
                }
            }
        } else {
            /* No config file — try default model path */
            const char *data_dir = typio_instance_get_data_dir(instance);
            if (data_dir) {
                char model_path[512];
                snprintf(model_path, sizeof(model_path),
                         "%s/whisper/ggml-base.bin", data_dir);
                struct whisper_context_params cparams =
                    whisper_context_default_params();
                svc->whisper_ctx =
                    whisper_init_from_file_with_params(model_path, cparams);
                if (svc->whisper_ctx) {
                    typio_log(TYPIO_LOG_INFO,
                              "Whisper model loaded: %s", model_path);
                } else {
                    typio_log(TYPIO_LOG_WARNING,
                              "Whisper model not found: %s "
                              "(voice input disabled)", model_path);
                }
            }
        }
    }

    if (!svc->whisper_ctx) {
        /* Model not available — service exists but is_available returns false */
        return svc;
    }

    /* Create eventfd for thread notification */
    svc->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (svc->event_fd < 0) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create eventfd");
        whisper_free(svc->whisper_ctx);
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
        whisper_free(svc->whisper_ctx);
        pthread_mutex_destroy(&svc->buffer_mutex);
        pthread_mutex_destroy(&svc->result_mutex);
        free(svc);
        return nullptr;
    }

    typio_log(TYPIO_LOG_INFO, "Voice service initialized (language=%s)",
              svc->language);
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

    if (svc->whisper_ctx) {
        whisper_free(svc->whisper_ctx);
    }

    free(svc->audio_buffer);
    free(svc->result);
    pthread_mutex_destroy(&svc->buffer_mutex);
    pthread_mutex_destroy(&svc->result_mutex);
    free(svc);
}

bool typio_voice_service_start(TypioVoiceService *svc) {
    if (!svc || !svc->whisper_ctx || svc->state != TYPIO_VOICE_IDLE) {
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
        /* Trim leading whitespace (whisper sometimes adds a leading space) */
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
    return svc && svc->whisper_ctx && svc->capture;
}
