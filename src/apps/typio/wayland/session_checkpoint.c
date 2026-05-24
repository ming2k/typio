/**
 * @file session_checkpoint.c
 * @brief Durable composition checkpoint — effectful glue (see header)
 */

#include "session_checkpoint.h"

#include "checkpoint_codec.h"
#include "monotonic_time.h"
#include "wl_frontend_internal.h"
#include "typio/engine.h"
#include "typio/engine_manager.h"
#include "typio/input_context.h"
#include "typio/instance.h"
#include "typio/log.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Concatenate a + b into a malloc'd string. Replaces asprintf, which is a
 * GNU/BSD extension while the project builds POSIX-only. Fixed format
 * literal keeps -Wformat-nonliteral quiet. */
static char *join2(const char *a, const char *b) {
    char tmp[PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s%s", a, b);
    if (n < 0 || (size_t)n >= sizeof(tmp))
        return nullptr;
    char *out = malloc((size_t)n + 1);
    if (!out)
        return nullptr;
    memcpy(out, tmp, (size_t)n + 1);
    return out;
}

/* A daemon restart is expected to be near-instant; 5 minutes is a generous
 * upper bound that still rejects checkpoints from a much earlier session. */
#define TYPIO_CKP_MAX_AGE_MS (5ULL * 60ULL * 1000ULL)

static TypioEngine *checkpoint_active_engine(TypioWlFrontend *frontend) {
    TypioEngineManager *manager;

    if (!frontend || !frontend->instance)
        return nullptr;

    manager = typio_instance_get_engine_manager(frontend->instance);
    return manager ? typio_engine_manager_get_active(manager) : nullptr;
}

/* Build "$XDG_RUNTIME_DIR/typio/session.ckpt", creating the typio dir.
 * Falls back to /tmp when XDG_RUNTIME_DIR is unset. Returns malloc'd path
 * or NULL. */
static char *checkpoint_path(void) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    char dir[PATH_MAX];
    char *path;
    int n;

    if (!runtime || !runtime[0])
        runtime = "/tmp";

    n = snprintf(dir, sizeof(dir), "%s/typio", runtime);
    if (n < 0 || (size_t)n >= sizeof(dir))
        return nullptr;

    if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
        typio_log(TYPIO_LOG_WARNING,
                  "Checkpoint dir %s unavailable: %s", dir, strerror(errno));
        return nullptr;
    }

    path = join2(dir, "/session.ckpt");
    return path;
}

static int checkpoint_write_atomic(const char *path, const char *buf, size_t size) {
    char *tmp;
    int fd;
    ssize_t written;

    tmp = join2(path, ".tmp");
    if (!tmp)
        return -1;

    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        typio_log(TYPIO_LOG_WARNING, "Checkpoint write open failed: %s",
                  strerror(errno));
        free(tmp);
        return -1;
    }

    written = write(fd, buf, size);
    if (written < 0 || (size_t)written != size) {
        typio_log(TYPIO_LOG_WARNING, "Checkpoint write short/failed: %s",
                  strerror(errno));
        close(fd);
        unlink(tmp);
        free(tmp);
        return -1;
    }

    /* Deliberately no fsync: this write runs in the UI flush path on every
     * preedit delta, and the checkpoint only needs to survive a *process*
     * crash, not power loss (it is discarded across reboot anyway). The
     * atomic rename below gives crash-consistency; data lives in the page
     * cache which a process crash preserves. fsync here would risk stalling
     * the event loop on a disk-backed $TMPDIR fallback. */
    close(fd);

    if (rename(tmp, path) != 0) {
        typio_log(TYPIO_LOG_WARNING, "Checkpoint rename failed: %s",
                  strerror(errno));
        unlink(tmp);
        free(tmp);
        return -1;
    }

    free(tmp);
    return 0;
}

/* Read entire file into a malloc'd buffer. Returns buffer + sets *out_size,
 * or NULL (no file or error). */
static char *checkpoint_read_all(const char *path, size_t *out_size) {
    int fd;
    struct stat st;
    char *buf;
    ssize_t got;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return nullptr;

    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > (off_t)(1 << 20)) {
        close(fd);
        return nullptr;
    }

    buf = malloc((size_t)st.st_size);
    if (!buf) {
        close(fd);
        return nullptr;
    }

    got = read(fd, buf, (size_t)st.st_size);
    close(fd);
    if (got < 0 || (size_t)got != (size_t)st.st_size) {
        free(buf);
        return nullptr;
    }

    *out_size = (size_t)st.st_size;
    return buf;
}

void typio_wl_session_checkpoint_discard(TypioWlFrontend *frontend) {
    char *path;

    (void)frontend;
    path = checkpoint_path();
    if (!path)
        return;
    if (unlink(path) != 0 && errno != ENOENT) {
        typio_log(TYPIO_LOG_DEBUG, "Checkpoint discard unlink: %s",
                  strerror(errno));
    }
    free(path);
}

void typio_wl_session_checkpoint_save(TypioWlFrontend *frontend) {
    TypioEngine *engine;
    char *blob = nullptr;
    size_t blob_size = 0;
    TypioCkpRecord rec;
    char *out;
    size_t out_size = 0;
    char *path;

    if (!frontend || !frontend->session || !frontend->session->ctx)
        return;

    engine = checkpoint_active_engine(frontend);
    if (!engine || !typio_engine_has_session_ops(engine))
        return;

    if (typio_engine_snapshot_session(engine, frontend->session->ctx,
                                      &blob, &blob_size) != TYPIO_OK)
        return;

    /* Nothing in flight: drop any stale checkpoint and stop. */
    if (!blob || blob_size == 0) {
        free(blob);
        typio_wl_session_checkpoint_discard(frontend);
        return;
    }

    rec.version = TYPIO_CKP_VERSION;
    rec.engine_name = typio_engine_get_name(engine);
    rec.engine_name_len = 0; /* ignored by encode */
    rec.identity = frontend->current_identity.stable_key
                       ? frontend->current_identity.stable_key : "";
    rec.identity_len = 0; /* ignored by encode */
    rec.boottime_ms = typio_wl_boottime_ms();
    rec.blob = blob;
    rec.blob_size = blob_size;

    if (!rec.engine_name) {
        free(blob);
        return;
    }

    out = typio_ckp_encode(&rec, &out_size);
    free(blob);
    if (!out)
        return;

    path = checkpoint_path();
    if (path) {
        checkpoint_write_atomic(path, out, out_size);
        free(path);
    }
    free(out);
}

bool typio_wl_session_checkpoint_try_restore(TypioWlFrontend *frontend) {
    TypioEngine *engine;
    char *path;
    char *buf;
    size_t size = 0;
    TypioCkpRecord rec;
    bool restored = false;

    if (!frontend || !frontend->session || !frontend->session->ctx)
        return false;

    engine = checkpoint_active_engine(frontend);
    if (!engine || !typio_engine_has_session_ops(engine))
        return false;

    path = checkpoint_path();
    if (!path)
        return false;

    buf = checkpoint_read_all(path, &size);
    if (!buf) {
        free(path);
        return false;
    }

    if (typio_ckp_decode(buf, size, &rec) &&
        typio_ckp_is_valid(&rec, typio_engine_get_name(engine),
                           typio_wl_boottime_ms(), TYPIO_CKP_MAX_AGE_MS)) {
        if (rec.blob && rec.blob_size > 0 &&
            typio_engine_restore_session(engine, frontend->session->ctx,
                                         rec.blob, rec.blob_size) == TYPIO_OK) {
            restored = true;
            typio_log(TYPIO_LOG_INFO,
                      "Restored composition checkpoint for engine %s (%zu bytes)",
                      typio_engine_get_name(engine), rec.blob_size);
        }
    }

    free(buf);

    /* Never replay the same checkpoint twice, whatever the outcome. */
    if (unlink(path) != 0 && errno != ENOENT)
        typio_log(TYPIO_LOG_DEBUG, "Checkpoint post-restore unlink: %s",
                  strerror(errno));
    free(path);
    return restored;
}
