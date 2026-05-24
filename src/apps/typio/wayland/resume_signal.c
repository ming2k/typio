/**
 * @file resume_signal.c
 * @brief System-resume detector implementation
 */

#include "resume_signal.h"

#include "monotonic_time.h"
#include "resume_model.h"
#include "typio_build_config.h"
#include "typio/log.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_STATUS_BUS
#  include <dbus/dbus.h>
#endif

/*
 * A boottime/monotonic delta below this is treated as normal scheduler
 * jitter (debugger pauses, very long stop-the-world GCs in dependent
 * libraries, brief container freezes). Above it, we conclude the kernel
 * suspended the process. 2000 ms is comfortably larger than any
 * legitimate single-tick pause but small enough to catch even very short
 * S3 cycles on modern hardware.
 */
#define TYPIO_WL_RESUME_GAP_THRESHOLD_MS 2000ULL

/*
 * After firing, ignore further fires for this many ms. Lets logind and
 * the gap detector coincide cleanly: whichever observes the resume first
 * triggers the callback, the other sees the cooldown and stays silent.
 */
#define TYPIO_WL_RESUME_COOLDOWN_MS 5000ULL

#ifdef HAVE_STATUS_BUS
#  define TYPIO_LOGIND_MATCH                                          \
       "type='signal',"                                                \
       "interface='org.freedesktop.login1.Manager',"                   \
       "member='PrepareForSleep',"                                     \
       "path='/org/freedesktop/login1'"
#endif

struct TypioWlResumeSignal {
    TypioWlResumeCallback cb;
    void *user_data;

    uint64_t last_monotonic_ms;
    uint64_t last_boottime_ms;
    uint64_t last_fire_monotonic_ms;

#ifdef HAVE_STATUS_BUS
    DBusConnection *conn;
    bool filter_added;
#endif
};

static void resume_signal_fire(TypioWlResumeSignal *rs,
                               const char *reason,
                               uint64_t sleep_ms) {
    uint64_t now_ms;

    if (!rs)
        return;

    now_ms = typio_wl_monotonic_ms();

    /* Cooldown: if either detector already fired recently, drop this
     * one. Use monotonic, not boottime, so the cooldown window itself
     * is measured in active time. */
    if (typio_wl_resume_in_cooldown(now_ms, rs->last_fire_monotonic_ms,
                                    TYPIO_WL_RESUME_COOLDOWN_MS)) {
        typio_log(TYPIO_LOG_DEBUG,
                  "Resume signal suppressed by cooldown: reason=%s sleep_ms=%" PRIu64,
                  reason ? reason : "unknown",
                  sleep_ms);
        return;
    }

    rs->last_fire_monotonic_ms = now_ms;
    typio_log(TYPIO_LOG_INFO,
              "Resume detected: source=%s sleep_ms=%" PRIu64,
              reason ? reason : "unknown",
              sleep_ms);

    if (rs->cb)
        rs->cb(rs->user_data, reason, sleep_ms);
}

#ifdef HAVE_STATUS_BUS
static DBusHandlerResult resume_signal_filter(DBusConnection *conn,
                                              DBusMessage *msg,
                                              void *user_data) {
    TypioWlResumeSignal *rs = user_data;
    dbus_bool_t going_to_sleep;
    DBusError err;

    (void)conn;

    if (!dbus_message_is_signal(msg,
                                "org.freedesktop.login1.Manager",
                                "PrepareForSleep")) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_error_init(&err);
    if (!dbus_message_get_args(msg, &err,
                                DBUS_TYPE_BOOLEAN, &going_to_sleep,
                                DBUS_TYPE_INVALID)) {
        typio_log(TYPIO_LOG_WARNING,
                  "PrepareForSleep signal missing boolean arg: %s",
                  err.message ? err.message : "(no detail)");
        dbus_error_free(&err);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (going_to_sleep) {
        /* Pre-sleep notification. We could use an inhibitor lock here
         * to flush state before suspend completes, but a minimal Stage 1
         * implementation just logs and waits for the resume edge. The
         * gap detector covers any compositor that doesn't redeliver
         * focus events on wake. */
        typio_log(TYPIO_LOG_INFO, "logind: system preparing to sleep");
    } else {
        resume_signal_fire(rs, "logind", 0);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

static bool resume_signal_init_dbus(TypioWlResumeSignal *rs) {
    DBusError err;

    dbus_error_init(&err);
    rs->conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
    if (!rs->conn || dbus_error_is_set(&err)) {
        typio_log(TYPIO_LOG_INFO,
                  "logind resume-signal disabled (system bus unavailable: %s)",
                  err.message ? err.message : "no detail");
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        rs->conn = nullptr;
        return false;
    }

    /* Don't let a system-bus disconnect terminate the daemon — logind
     * coming and going must not take the IME with it. */
    dbus_connection_set_exit_on_disconnect(rs->conn, FALSE);

    dbus_bus_add_match(rs->conn, TYPIO_LOGIND_MATCH, &err);
    if (dbus_error_is_set(&err)) {
        typio_log(TYPIO_LOG_WARNING,
                  "Failed to add PrepareForSleep match rule: %s",
                  err.message);
        dbus_error_free(&err);
        dbus_connection_close(rs->conn);
        dbus_connection_unref(rs->conn);
        rs->conn = nullptr;
        return false;
    }

    if (!dbus_connection_add_filter(rs->conn, resume_signal_filter, rs, nullptr)) {
        typio_log(TYPIO_LOG_WARNING, "Failed to add PrepareForSleep filter");
        dbus_bus_remove_match(rs->conn, TYPIO_LOGIND_MATCH, nullptr);
        dbus_connection_close(rs->conn);
        dbus_connection_unref(rs->conn);
        rs->conn = nullptr;
        return false;
    }
    rs->filter_added = true;
    typio_log(TYPIO_LOG_INFO,
              "logind PrepareForSleep subscriber active");
    return true;
}

static void resume_signal_teardown_dbus(TypioWlResumeSignal *rs) {
    if (!rs->conn)
        return;

    if (rs->filter_added) {
        dbus_connection_remove_filter(rs->conn, resume_signal_filter, rs);
        rs->filter_added = false;
    }
    dbus_bus_remove_match(rs->conn, TYPIO_LOGIND_MATCH, nullptr);
    dbus_connection_close(rs->conn);
    dbus_connection_unref(rs->conn);
    rs->conn = nullptr;
}
#endif /* HAVE_STATUS_BUS */

TypioWlResumeSignal *typio_wl_resume_signal_create(TypioWlResumeCallback cb,
                                                   void *user_data) {
    TypioWlResumeSignal *rs;

    rs = calloc(1, sizeof(*rs));
    if (!rs)
        return nullptr;

    rs->cb = cb;
    rs->user_data = user_data;
    rs->last_monotonic_ms = typio_wl_monotonic_ms();
    rs->last_boottime_ms = typio_wl_boottime_ms();

#ifdef HAVE_STATUS_BUS
    (void)resume_signal_init_dbus(rs); /* falls back to gap detector on failure */
#endif

    return rs;
}

void typio_wl_resume_signal_destroy(TypioWlResumeSignal *rs) {
    if (!rs)
        return;

#ifdef HAVE_STATUS_BUS
    resume_signal_teardown_dbus(rs);
#endif

    free(rs);
}

int typio_wl_resume_signal_get_fd(TypioWlResumeSignal *rs) {
#ifdef HAVE_STATUS_BUS
    int fd = -1;
    if (rs && rs->conn && dbus_connection_get_unix_fd(rs->conn, &fd))
        return fd;
#endif
    (void)rs;
    return -1;
}

int typio_wl_resume_signal_dispatch(TypioWlResumeSignal *rs) {
#ifdef HAVE_STATUS_BUS
    int dispatched = 0;

    if (!rs || !rs->conn)
        return 0;

    /* Non-blocking read; bounded drain so a flood on the system bus can't
     * spin the event loop (mirrors the status-bus per-tick cap). */
    dbus_connection_read_write(rs->conn, 0);
    while (dispatched < 16 &&
           dbus_connection_dispatch(rs->conn) == DBUS_DISPATCH_DATA_REMAINS) {
        dispatched++;
    }
#endif
    (void)rs;
    return 0;
}

void typio_wl_resume_signal_tick(TypioWlResumeSignal *rs) {
    uint64_t mono_now;
    uint64_t boot_now;
    uint64_t mono_delta;
    uint64_t boot_delta;
    uint64_t gap;

    if (!rs)
        return;

    mono_now = typio_wl_monotonic_ms();
    boot_now = typio_wl_boottime_ms();

    if (rs->last_monotonic_ms == 0 || rs->last_boottime_ms == 0) {
        rs->last_monotonic_ms = mono_now;
        rs->last_boottime_ms = boot_now;
        return;
    }

    mono_delta = (mono_now >= rs->last_monotonic_ms)
                     ? mono_now - rs->last_monotonic_ms : 0;
    boot_delta = (boot_now >= rs->last_boottime_ms)
                     ? boot_now - rs->last_boottime_ms : 0;

    rs->last_monotonic_ms = mono_now;
    rs->last_boottime_ms = boot_now;

    if (typio_wl_resume_gap_exceeded(mono_delta, boot_delta,
                                     TYPIO_WL_RESUME_GAP_THRESHOLD_MS, &gap)) {
        resume_signal_fire(rs, "boottime_gap", gap);
    }
}
