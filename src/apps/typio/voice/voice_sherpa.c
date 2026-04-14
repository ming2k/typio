/**
 * @file voice_sherpa.c
 * @brief Sherpa-ONNX speech-to-text backend
 *
 * Supports multiple model architectures: SenseVoice, Whisper (ONNX),
 * Zipformer (transducer), Paraformer, NeMo CTC, and more.
 *
 * Model directory layout under ${data_dir}/sherpa-onnx/${model}/:
 *   Auto-detected from files present.  Standard sherpa-onnx archives
 *   extract with their original naming (e.g. tiny-encoder.onnx,
 *   model.int8.onnx, tokens.txt).
 */

#include "voice_backend.h"
#include "utils/log.h"

#include <sherpa-onnx/c-api/c-api.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

typedef struct {
    TypioVoiceBackend base;
    const SherpaOnnxOfflineRecognizer *recognizer;
} SherpaBackend;

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/**
 * Find a file in directory matching a suffix (e.g. "-encoder.onnx").
 * Writes the full path to buf.  Returns true if found.
 */
static bool find_file_with_suffix(const char *dir, const char *suffix,
                                   char *buf, size_t buf_size) {
    DIR *d = opendir(dir);
    if (!d) {
        return false;
    }

    size_t suf_len = strlen(suffix);
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t name_len = strlen(ent->d_name);
        if (name_len >= suf_len &&
            strcmp(ent->d_name + name_len - suf_len, suffix) == 0) {
            snprintf(buf, buf_size, "%s/%s", dir, ent->d_name);
            closedir(d);
            return true;
        }
    }
    closedir(d);
    return false;
}

/**
 * Find a file matching an exact name, or falling back to a suffix pattern.
 */
static bool find_model_file(const char *dir, const char *exact,
                              const char *suffix_fallback,
                              char *buf, size_t buf_size) {
    snprintf(buf, buf_size, "%s/%s", dir, exact);
    if (file_exists(buf)) {
        return true;
    }
    if (suffix_fallback) {
        return find_file_with_suffix(dir, suffix_fallback, buf, buf_size);
    }
    return false;
}

/* Model type detection */
typedef enum {
    SHERPA_MODEL_UNKNOWN = 0,
    SHERPA_MODEL_SENSE_VOICE,
    SHERPA_MODEL_WHISPER,
    SHERPA_MODEL_TRANSDUCER,
    SHERPA_MODEL_PARAFORMER,
} SherpaModelType;

static SherpaModelType detect_model_type(const char *model_dir,
                                           char *tokens_buf,
                                           char *file1_buf,
                                           char *file2_buf,
                                           char *file3_buf,
                                           size_t buf_size) {
    /* Find tokens file: tokens.txt or *-tokens.txt */
    if (!find_model_file(model_dir, "tokens.txt", "-tokens.txt",
                         tokens_buf, buf_size)) {
        return SHERPA_MODEL_UNKNOWN;
    }

    /* SenseVoice: model.int8.onnx or model.onnx, with no encoder/decoder */
    if (find_model_file(model_dir, "model.int8.onnx", NULL,
                        file1_buf, buf_size) ||
        find_model_file(model_dir, "model.onnx", NULL,
                        file1_buf, buf_size)) {
        /* Distinguish SenseVoice from Paraformer:
         * SenseVoice archives have "sense" in the directory name,
         * or we check whether an encoder.onnx also exists (whisper).
         * As a heuristic: if no encoder/decoder pair exists, and
         * the dir name contains "sense-voice", it's SenseVoice.
         * Otherwise treat as Paraformer. */
        if (strstr(model_dir, "sense-voice") ||
            strstr(model_dir, "sense_voice") ||
            strstr(model_dir, "sensevoice")) {
            return SHERPA_MODEL_SENSE_VOICE;
        }
        return SHERPA_MODEL_PARAFORMER;
    }

    /* Transducer: encoder + decoder + joiner */
    if (find_model_file(model_dir, "joiner.onnx", "-joiner.onnx",
                        file3_buf, buf_size)) {
        if (find_model_file(model_dir, "encoder.onnx", "-encoder.onnx",
                            file1_buf, buf_size) &&
            find_model_file(model_dir, "decoder.onnx", "-decoder.onnx",
                            file2_buf, buf_size)) {
            return SHERPA_MODEL_TRANSDUCER;
        }
    }

    /* Whisper: encoder + decoder, no joiner */
    if (find_model_file(model_dir, "encoder.onnx", "-encoder.onnx",
                        file1_buf, buf_size) &&
        find_model_file(model_dir, "decoder.onnx", "-decoder.onnx",
                        file2_buf, buf_size)) {
        return SHERPA_MODEL_WHISPER;
    }

    return SHERPA_MODEL_UNKNOWN;
}

static const char *model_type_name(SherpaModelType type) {
    switch (type) {
    case SHERPA_MODEL_SENSE_VOICE: return "sense_voice";
    case SHERPA_MODEL_WHISPER:     return "whisper";
    case SHERPA_MODEL_TRANSDUCER:  return "transducer";
    case SHERPA_MODEL_PARAFORMER:  return "paraformer";
    default:                       return "unknown";
    }
}

static char *sherpa_process(TypioVoiceBackend *backend,
                             const float *samples, size_t n_samples) {
    SherpaBackend *sb = (SherpaBackend *)backend;

    const SherpaOnnxOfflineStream *stream =
        SherpaOnnxCreateOfflineStream(sb->recognizer);
    if (!stream) {
        typio_log(TYPIO_LOG_ERROR, "sherpa-onnx: failed to create stream");
        return NULL;
    }

    SherpaOnnxAcceptWaveformOffline(stream, 16000, samples, (int32_t)n_samples);
    SherpaOnnxDecodeOfflineStream(sb->recognizer, stream);

    const SherpaOnnxOfflineRecognizerResult *r =
        SherpaOnnxGetOfflineStreamResult(stream);

    char *result = NULL;
    if (r && r->text && r->text[0] != '\0') {
        result = strdup(r->text);
    }

    if (r) {
        SherpaOnnxDestroyOfflineRecognizerResult(r);
    }
    SherpaOnnxDestroyOfflineStream(stream);

    return result;
}

static void sherpa_destroy(TypioVoiceBackend *backend) {
    SherpaBackend *sb = (SherpaBackend *)backend;
    if (sb->recognizer) {
        SherpaOnnxDestroyOfflineRecognizer(sb->recognizer);
    }
    free(sb);
}

static const TypioVoiceBackendOps sherpa_ops = {
    .process = sherpa_process,
    .destroy = sherpa_destroy,
};

/**
 * When no explicit model is given, scan the sherpa-onnx directory
 * for the first subdirectory that contains a recognizable model.
 */
static bool auto_detect_model_dir(const char *base_dir,
                                  char *model_dir, size_t dir_size,
                                  char *tokens_buf, char *f1, char *f2,
                                  char *f3, size_t buf_size,
                                  SherpaModelType *out_type) {
    DIR *d = opendir(base_dir);
    if (!d) {
        return false;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        snprintf(model_dir, dir_size, "%s/%s", base_dir, ent->d_name);

        struct stat st;
        if (stat(model_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }

        *out_type = detect_model_type(model_dir, tokens_buf, f1, f2, f3,
                                      buf_size);
        if (*out_type != SHERPA_MODEL_UNKNOWN) {
            closedir(d);
            return true;
        }
    }
    closedir(d);
    return false;
}

TypioVoiceBackend *typio_voice_backend_sherpa_new(const char *data_dir,
                                                   const char *language,
                                                   const char *model) {
    if (!data_dir) {
        return NULL;
    }

    char base_dir[512];
    snprintf(base_dir, sizeof(base_dir), "%s/sherpa-onnx", data_dir);

    char model_dir[512];
    char tokens_path[512] = {};
    char file1[512] = {};  /* encoder or model */
    char file2[512] = {};  /* decoder */
    char file3[512] = {};  /* joiner */
    SherpaModelType type = SHERPA_MODEL_UNKNOWN;

    if (model && *model) {
        /* Explicit model name */
        int ret = snprintf(model_dir, sizeof(model_dir), "%s/%s", base_dir, model);
        if (ret < 0 || (size_t)ret >= sizeof(model_dir)) {
            typio_log(TYPIO_LOG_WARNING, "sherpa-onnx: model path too long");
            return NULL;
        }
        type = detect_model_type(model_dir, tokens_path, file1, file2, file3,
                                 sizeof(tokens_path));
    }

    if (type == SHERPA_MODEL_UNKNOWN) {
        /* Auto-scan: find first usable model directory */
        if (!auto_detect_model_dir(base_dir, model_dir, sizeof(model_dir),
                                   tokens_path, file1, file2, file3,
                                   sizeof(tokens_path), &type)) {
            typio_log(TYPIO_LOG_WARNING,
                      "sherpa-onnx: no recognizable model in %s", base_dir);
            return NULL;
        }
    }

    typio_log(TYPIO_LOG_INFO,
              "sherpa-onnx: detected model type '%s' in %s",
              model_type_name(type), model_dir);

    /* Build config */
    SherpaOnnxOfflineRecognizerConfig config;
    memset(&config, 0, sizeof(config));

    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    config.model_config.num_threads = 4;
    config.model_config.debug = 0;
    config.model_config.provider = "cpu";
    config.model_config.tokens = tokens_path;
    config.decoding_method = "greedy_search";

    switch (type) {
    case SHERPA_MODEL_SENSE_VOICE:
        config.model_config.sense_voice.model = file1;
        config.model_config.sense_voice.language = language ? language : "auto";
        config.model_config.sense_voice.use_itn = 1;
        break;

    case SHERPA_MODEL_WHISPER:
        config.model_config.whisper.encoder = file1;
        config.model_config.whisper.decoder = file2;
        config.model_config.whisper.language = language; /* NULL = auto */
        config.model_config.whisper.task = "transcribe";
        break;

    case SHERPA_MODEL_TRANSDUCER:
        config.model_config.transducer.encoder = file1;
        config.model_config.transducer.decoder = file2;
        config.model_config.transducer.joiner = file3;
        break;

    case SHERPA_MODEL_PARAFORMER:
        config.model_config.paraformer.model = file1;
        break;

    default:
        return NULL;
    }

    const SherpaOnnxOfflineRecognizer *recognizer =
        SherpaOnnxCreateOfflineRecognizer(&config);
    if (!recognizer) {
        typio_log(TYPIO_LOG_WARNING,
                  "sherpa-onnx: failed to create recognizer from %s",
                  model_dir);
        return NULL;
    }

    typio_log(TYPIO_LOG_INFO,
              "sherpa-onnx: recognizer created (type=%s, dir=%s)",
              model_type_name(type), model_dir);

    SherpaBackend *sb = calloc(1, sizeof(SherpaBackend));
    if (!sb) {
        SherpaOnnxDestroyOfflineRecognizer(recognizer);
        return NULL;
    }

    sb->base.ops = &sherpa_ops;
    sb->recognizer = recognizer;

    return &sb->base;
}
