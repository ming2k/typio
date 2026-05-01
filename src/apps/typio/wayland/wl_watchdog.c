/**
 * @file wl_watchdog.c
 * @brief Watchdog thread and stage tracking for Wayland frontend
 */

#include "wl_frontend_internal.h"
#include "wl_trace.h"
#include "monotonic_time.h"
#include "utils/log.h"

#include <inttypes.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

static const char *stage_name(TypioWlLoopStage stage) {
    switch (stage) {
    case TYPIO_WL_LOOP_STAGE_IDLE:              return "idle";
    case TYPIO_WL_LOOP_STAGE_POPUP_UPDATE:      return "popup_update";
    case TYPIO_WL_LOOP_STAGE_PREPARE_READ:      return "prepare_read";
    case TYPIO_WL_LOOP_STAGE_FLUSH:             return "flush";
    case TYPIO_WL_LOOP_STAGE_POLL:              return "poll";
    case TYPIO_WL_LOOP_STAGE_READ_EVENTS:       return "read_events";
    case TYPIO_WL_LOOP_STAGE_DISPATCH_PENDING:  return "dispatch_pending";
    case TYPIO_WL_LOOP_STAGE_AUX_IO:            return "aux_io";
    case TYPIO_WL_LOOP_STAGE_REPEAT:            return "repeat";
    case TYPIO_WL_LOOP_STAGE_CONFIG_RELOAD:     return "config_reload";
    default:                                    return "unknown";
    }
}

void typio_wl_frontend_watchdog_heartbeat(TypioWlFrontend *frontend) {
    if (!frontend) return;
    uint64_t now = typio_wl_monotonic_ms();
    atomic_store(&frontend->watchdog_heartbeat_ms, now);
    atomic_store(&frontend->watchdog_stage_since_ms, now);
}

void typio_wl_frontend_watchdog_set_stage(TypioWlFrontend *frontend,
                                           TypioWlLoopStage stage) {
    if (!frontend) return;
    atomic_store(&frontend->watchdog_loop_stage, (int)stage);
    atomic_store(&frontend->watchdog_stage_since_ms, typio_wl_monotonic_ms());
}

static uint32_t runtime_age_ms(uint64_t now_ms, uint64_t start_ms) {
    return (now_ms >= start_ms) ? (uint32_t)(now_ms - start_ms) : 0;
}

static int32_t runtime_deadline_remaining_ms(uint64_t now_ms,
                                              uint64_t deadline_ms) {
    if (deadline_ms == 0) return -1;
    return (deadline_ms > now_ms) ? (int32_t)(deadline_ms - now_ms) : 0;
}

static void *watchdog_thread(void *data) {
    TypioWlFrontend *frontend = data;
    uint64_t last_heartbeat_ms = 0;
    int last_stage = TYPIO_WL_LOOP_STAGE_IDLE;
    uint64_t last_stage_since_ms = 0;

    typio_log(TYPIO_LOG_DEBUG, "Watchdog thread started");

    while (!atomic_load(&frontend->watchdog_stop)) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
        nanosleep(&ts, nullptr);

        if (!atomic_load(&frontend->watchdog_armed))
            continue;

        uint64_t heartbeat_ms = atomic_load(&frontend->watchdog_heartbeat_ms);
        int stage = atomic_load(&frontend->watchdog_loop_stage);
        uint64_t stage_since_ms = atomic_load(&frontend->watchdog_stage_since_ms);

        if (heartbeat_ms == last_heartbeat_ms && stage == last_stage &&
            stage_since_ms == last_stage_since_ms) {
            uint64_t now = typio_wl_monotonic_ms();
            uint32_t stuck_ms = runtime_age_ms(now, heartbeat_ms);
            int32_t deadline_remaining = runtime_deadline_remaining_ms(
                now, frontend->virtual_keyboard_keymap_deadline_ms);

            if (stuck_ms >= 3000) {
                typio_log(TYPIO_LOG_ERROR,
                    "Watchdog: loop stuck for %u ms in stage=%s "
                    "vk_state=%s vk_deadline=%d ms age=%u ms",
                    stuck_ms, stage_name(stage),
                    typio_wl_vk_state_name(frontend->virtual_keyboard_state),
                    deadline_remaining,
                    runtime_age_ms(now, frontend->virtual_keyboard_state_since_ms));
                typio_log_dump_recent_to_configured_path();
                kill(getpid(), SIGKILL);
                break;
            }
        }

        last_heartbeat_ms = heartbeat_ms;
        last_stage = stage;
        last_stage_since_ms = stage_since_ms;
    }

    typio_log(TYPIO_LOG_DEBUG, "Watchdog thread stopped");
    return nullptr;
}

void typio_wl_frontend_watchdog_start(TypioWlFrontend *frontend) {
    if (!frontend || frontend->watchdog_thread_started) return;

    atomic_store(&frontend->watchdog_stop, false);
    atomic_store(&frontend->watchdog_armed, false);
    frontend->watchdog_heartbeat_ms = 0;
    frontend->watchdog_stage_since_ms = 0;
    frontend->watchdog_loop_stage = TYPIO_WL_LOOP_STAGE_IDLE;

    if (pthread_create(&frontend->watchdog_thread, nullptr,
                       watchdog_thread, frontend) != 0) {
        typio_log(TYPIO_LOG_WARNING, "Failed to start watchdog thread");
        return;
    }
    frontend->watchdog_thread_started = true;
}

void typio_wl_frontend_watchdog_stop(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->watchdog_thread_started) return;

    atomic_store(&frontend->watchdog_stop, true);
    pthread_join(frontend->watchdog_thread, nullptr);
    frontend->watchdog_thread_started = false;
}
