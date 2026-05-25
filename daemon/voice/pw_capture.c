/**
 * @file pw_capture.c
 * @brief PipeWire audio capture - 16kHz mono float32 PCM
 */

#define _GNU_SOURCE

#include "pw_capture.h"
#include "typio/log.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#pragma GCC diagnostic pop
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TYPIO_PW_SAMPLE_RATE 16000
#define TYPIO_PW_CHANNELS 1

struct TypioPwCapture {
    TypioAudioSource base;      /* Must be first field for casting */

    struct pw_thread_loop *loop;
    struct pw_stream *stream;

    TypioPwCaptureCallback callback;
    void *user_data;

    bool capturing;
    uint32_t frames_received;  /* diagnostic counter */
};

static void on_state_changed(void *data, enum pw_stream_state old,
                              enum pw_stream_state state, const char *error) {
    TypioPwCapture *cap = data;
    typio_log(TYPIO_LOG_INFO, "PipeWire stream state: %s -> %s%s%s",
              pw_stream_state_as_string(old),
              pw_stream_state_as_string(state),
              error ? " error=" : "",
              error ? error : "");

    if (state == PW_STREAM_STATE_ERROR && cap) {
        cap->capturing = false;
        typio_log(TYPIO_LOG_ERROR, "PipeWire stream error, capture halted");
    }
}

static void on_process(void *data) {
    TypioPwCapture *cap = data;
    struct pw_buffer *buf;
    struct spa_buffer *spa_buf;
    const float *samples;
    const struct spa_chunk *chunk;
    uint32_t n_samples;

    if (!cap->capturing) {
        return;
    }

    buf = pw_stream_dequeue_buffer(cap->stream);
    if (!buf) {
        return;
    }

    spa_buf = buf->buffer;
    if (!spa_buf->datas[0].data || !spa_buf->datas[0].chunk) {
        pw_stream_queue_buffer(cap->stream, buf);
        return;
    }

    chunk = spa_buf->datas[0].chunk;
    if (chunk->size == 0 ||
        (chunk->offset % sizeof(float)) != 0 ||
        (chunk->size % sizeof(float)) != 0 ||
        chunk->offset > spa_buf->datas[0].maxsize ||
        chunk->size > spa_buf->datas[0].maxsize - chunk->offset) {
        pw_stream_queue_buffer(cap->stream, buf);
        return;
    }

    samples = (const float *)((const uint8_t *)spa_buf->datas[0].data +
                              chunk->offset);
    n_samples = chunk->size / sizeof(float);

    if (n_samples > 0) {
        cap->frames_received++;
        if (cap->frames_received == 1) {
            typio_log(TYPIO_LOG_INFO,
                      "PipeWire: first audio buffer received (%u samples)", n_samples);
        }
        if (cap->callback) {
            cap->callback(samples, (size_t)n_samples, cap->user_data);
        }
    }

    pw_stream_queue_buffer(cap->stream, buf);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state_changed,
    .process = on_process,
};

/* Forward declarations for TypioAudioSourceOps vtable */
static bool pw_cap_start(TypioAudioSource *source);
static void pw_cap_stop(TypioAudioSource *source);
static void pw_cap_free(TypioAudioSource *source);
static int pw_cap_get_fd(TypioAudioSource *source);
static void pw_cap_dispatch(TypioAudioSource *source);

static const TypioAudioSourceOps pw_audio_ops = {
    .start    = pw_cap_start,
    .stop     = pw_cap_stop,
    .free     = pw_cap_free,
    .get_fd   = pw_cap_get_fd,
    .dispatch = pw_cap_dispatch,
};

TypioPwCapture *typio_pw_capture_new(TypioPwCaptureCallback cb,
                                     void *user_data) {
    TypioPwCapture *cap = calloc(1, sizeof(TypioPwCapture));
    if (!cap) {
        return nullptr;
    }

    cap->callback = cb;
    cap->user_data = user_data;

    pw_init(nullptr, nullptr);

    cap->loop = pw_thread_loop_new("typio-capture", nullptr);
    if (!cap->loop) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create PipeWire thread loop");
        free(cap);
        return nullptr;
    }

    /* Stream is created fresh on each typio_pw_capture_start call. */

    if (pw_thread_loop_start(cap->loop) < 0) {
        typio_log(TYPIO_LOG_ERROR, "Failed to start PipeWire thread loop");
        pw_thread_loop_destroy(cap->loop);
        free(cap);
        return nullptr;
    }

    cap->base.ops = &pw_audio_ops;

    typio_log(TYPIO_LOG_INFO, "PipeWire capture initialized");
    return cap;
}

static void pw_cap_free(TypioAudioSource *source) {
    TypioPwCapture *cap = (TypioPwCapture *)source;
    if (!cap) {
        return;
    }

    pw_thread_loop_lock(cap->loop);
    if (cap->stream) {
        pw_stream_destroy(cap->stream);
    }
    pw_thread_loop_unlock(cap->loop);

    pw_thread_loop_stop(cap->loop);
    pw_thread_loop_destroy(cap->loop);

    pw_deinit();
    free(cap);
}

void typio_pw_capture_free(TypioPwCapture *cap) {
    pw_cap_free(&cap->base);
}

static struct pw_stream *pw_capture_make_stream(TypioPwCapture *cap) {
    struct pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Communication",
        PW_KEY_NODE_LATENCY, "256/16000",
        PW_KEY_APP_NAME, "Typio",
        PW_KEY_NODE_NAME, "typio-voice-capture",
        nullptr);

    return pw_stream_new_simple(
        pw_thread_loop_get_loop(cap->loop),
        "typio-capture",
        props,
        &stream_events,
        cap);
}

static bool pw_cap_start(TypioAudioSource *source) {
    TypioPwCapture *cap = (TypioPwCapture *)source;
    if (!cap || cap->capturing) {
        return false;
    }

    pw_thread_loop_lock(cap->loop);

    /* Recreate stream each session — pw_stream_connect cannot be called
     * twice on the same stream object after a disconnect. */
    if (cap->stream) {
        pw_stream_destroy(cap->stream);
        cap->stream = nullptr;
    }
    cap->frames_received = 0;

    cap->stream = pw_capture_make_stream(cap);
    if (!cap->stream) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create PipeWire stream");
        pw_thread_loop_unlock(cap->loop);
        return false;
    }

    uint8_t params_buf[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(params_buf,
                                                     sizeof(params_buf));
    const struct spa_pod *params[1];

    struct spa_audio_info_raw info = {
        .format = SPA_AUDIO_FORMAT_F32,
        .rate = TYPIO_PW_SAMPLE_RATE,
        .channels = TYPIO_PW_CHANNELS,
    };
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    int ret = pw_stream_connect(
        cap->stream,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS,
        params, 1);

    if (ret < 0) {
        typio_log(TYPIO_LOG_ERROR, "Failed to connect PipeWire stream: %d", ret);
        pw_stream_destroy(cap->stream);
        cap->stream = nullptr;
        pw_thread_loop_unlock(cap->loop);
        return false;
    }

    cap->capturing = true;
    pw_thread_loop_unlock(cap->loop);

    typio_log(TYPIO_LOG_INFO, "PipeWire capture started (16kHz mono float32)");
    return true;
}

static void pw_cap_stop(TypioAudioSource *source) {
    TypioPwCapture *cap = (TypioPwCapture *)source;
    if (!cap || !cap->capturing) {
        return;
    }

    pw_thread_loop_lock(cap->loop);
    cap->capturing = false;
    if (cap->stream) {
        pw_stream_disconnect(cap->stream);
    }
    pw_thread_loop_unlock(cap->loop);

    typio_log(TYPIO_LOG_INFO, "PipeWire capture stopped (%u frames received)",
              cap->frames_received);
}

static int pw_cap_get_fd(TypioAudioSource *source) {
    TypioPwCapture *cap = (TypioPwCapture *)source;
    if (!cap || !cap->loop) {
        return -1;
    }
    /* With pw_thread_loop, audio processing happens on PipeWire's thread.
     * No external fd polling needed — return -1 to skip poll integration. */
    return -1;
}

static void pw_cap_dispatch([[maybe_unused]] TypioAudioSource *source) {
    /* With pw_thread_loop, dispatching is handled internally.
     * This is a no-op but kept for API completeness. */
}

int typio_pw_capture_get_fd(TypioPwCapture *cap) {
    return pw_cap_get_fd(&cap->base);
}

void typio_pw_capture_dispatch([[maybe_unused]] TypioPwCapture *cap) {
    pw_cap_dispatch(&cap->base);
}

bool typio_pw_capture_start(TypioPwCapture *cap) {
    return pw_cap_start(&cap->base);
}

void typio_pw_capture_stop(TypioPwCapture *cap) {
    pw_cap_stop(&cap->base);
}

TypioAudioSource *typio_pw_capture_as_audio_source(TypioPwCapture *cap) {
    return cap ? &cap->base : NULL;
}
