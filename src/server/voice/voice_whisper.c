/**
 * @file voice_whisper.c
 * @brief Whisper.cpp speech-to-text backend
 */

#include "voice_backend.h"
#include "utils/log.h"

#include <whisper.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    TypioVoiceBackend base;
    struct whisper_context *ctx;
    char language[16]; /* empty string = auto-detect */
} WhisperBackend;

static char *whisper_process(TypioVoiceBackend *backend,
                              const float *samples, size_t n_samples) {
    WhisperBackend *wb = (WhisperBackend *)backend;

    struct whisper_full_params params =
        whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_progress = false;
    params.print_special = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.single_segment = true;
    params.no_timestamps = true;
    params.language = wb->language[0] ? wb->language : "auto";

    int ret = whisper_full(wb->ctx, params, samples, (int)n_samples);
    if (ret != 0) {
        typio_log(TYPIO_LOG_ERROR, "Whisper inference failed: %d", ret);
        return NULL;
    }

    int n_segments = whisper_full_n_segments(wb->ctx);
    size_t total_len = 0;
    for (int i = 0; i < n_segments; i++) {
        const char *seg = whisper_full_get_segment_text(wb->ctx, i);
        if (seg) {
            total_len += strlen(seg);
        }
    }

    if (total_len == 0) {
        return NULL;
    }

    char *result = calloc(total_len + 1, sizeof(char));
    if (!result) {
        return NULL;
    }

    for (int i = 0; i < n_segments; i++) {
        const char *seg = whisper_full_get_segment_text(wb->ctx, i);
        if (seg) {
            strcat(result, seg);
        }
    }

    return result;
}

static void whisper_destroy(TypioVoiceBackend *backend) {
    WhisperBackend *wb = (WhisperBackend *)backend;
    if (wb->ctx) {
        whisper_free(wb->ctx);
    }
    free(wb);
}

static const TypioVoiceBackendOps whisper_ops = {
    .process = whisper_process,
    .destroy = whisper_destroy,
};

TypioVoiceBackend *typio_voice_backend_whisper_new(const char *data_dir,
                                                    const char *language,
                                                    const char *model) {
    if (!data_dir) {
        return NULL;
    }

    const char *model_name = model ? model : "base";

    char model_path[512];
    snprintf(model_path, sizeof(model_path),
             "%s/whisper/ggml-%s.bin", data_dir, model_name);

    struct whisper_context_params cparams = whisper_context_default_params();
    struct whisper_context *ctx =
        whisper_init_from_file_with_params(model_path, cparams);
    if (!ctx) {
        typio_log(TYPIO_LOG_WARNING,
                  "Whisper model not found: %s (backend disabled)", model_path);
        return NULL;
    }

    typio_log(TYPIO_LOG_INFO, "Whisper model loaded: %s", model_path);

    WhisperBackend *wb = calloc(1, sizeof(WhisperBackend));
    if (!wb) {
        whisper_free(ctx);
        return NULL;
    }

    wb->base.ops = &whisper_ops;
    wb->ctx = ctx;
    if (language) {
        strncpy(wb->language, language, sizeof(wb->language) - 1);
    }
    /* else: wb->language stays zeroed = auto-detect */

    return &wb->base;
}
