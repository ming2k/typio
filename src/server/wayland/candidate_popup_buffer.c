/**
 * @file candidate_popup_buffer.c
 * @brief Wayland SHM buffer pool for candidate popup rendering
 */

#include "candidate_popup_buffer.h"
#include "utils/log.h"

#include <cairo.h>
#include <wayland-client.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void buffer_release_handler(void *data,
                                   [[maybe_unused]] struct wl_buffer *buffer) {
    TypioCandidatePopupBuffer *popup_buffer = data;
    popup_buffer->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release_handler,
};

static int create_shm_file(size_t size) {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    char template_path[512];
    int fd;

    snprintf(template_path, sizeof(template_path), "%s/%s",
             (runtime_dir && runtime_dir[0]) ? runtime_dir : "/tmp",
             "typio-popup-XXXXXX");

    fd = mkstemp(template_path);
    if (fd < 0) {
        return -1;
    }

    unlink(template_path);

    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

void typio_candidate_popup_buffer_reset(TypioCandidatePopupBuffer *buffer) {
    if (!buffer) {
        return;
    }

    if (buffer->buffer) {
        wl_buffer_destroy(buffer->buffer);
    }
    if (buffer->data && buffer->size > 0) {
        munmap(buffer->data, buffer->size);
    }

    memset(buffer, 0, sizeof(*buffer));
}

bool typio_candidate_popup_buffer_create(TypioCandidatePopupBuffer *buffer,
                               struct wl_shm *shm,
                               int width, int height) {
    int stride;
    size_t size;
    int fd;
    void *data;
    struct wl_shm_pool *pool;

    typio_candidate_popup_buffer_reset(buffer);

    stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    size = (size_t)stride * (size_t)height;
    fd = create_shm_file(size);
    if (fd < 0) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create popup shm file: %s",
                  strerror(errno));
        return false;
    }

    data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        typio_log(TYPIO_LOG_ERROR, "Failed to mmap popup buffer: %s",
                  strerror(errno));
        close(fd);
        return false;
    }

    pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    close(fd);
    if (!pool) {
        munmap(data, size);
        return false;
    }

    buffer->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                                               WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    if (!buffer->buffer) {
        munmap(data, size);
        return false;
    }

    buffer->data = data;
    buffer->size = size;
    buffer->width = width;
    buffer->height = height;
    buffer->stride = stride;
    buffer->busy = false;
    wl_buffer_add_listener(buffer->buffer, &buffer_listener, buffer);
    return true;
}

TypioCandidatePopupBuffer *typio_candidate_popup_buffer_acquire(TypioCandidatePopupBuffer *pool,
                                             size_t pool_size,
                                             struct wl_shm *shm,
                                             int width, int height) {
    for (size_t i = 0; i < pool_size; ++i) {
        TypioCandidatePopupBuffer *buffer = &pool[i];

        if (buffer->busy) {
            continue;
        }

        if (buffer->buffer && buffer->width == width && buffer->height == height) {
            return buffer;
        }

        if (typio_candidate_popup_buffer_create(buffer, shm, width, height)) {
            return buffer;
        }
    }

    typio_log(TYPIO_LOG_WARNING, "No free popup buffer available");
    return nullptr;
}
