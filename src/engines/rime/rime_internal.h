/**
 * @file rime_internal.h
 * @brief Internal shared definitions for the Rime engine plugin
 *
 * This header declares data structures, constants, and function prototypes
 * used across all rime engine modules.  It should not be included by code
 * outside src/engines/rime/.
 */

#ifndef TYPIO_RIME_INTERNAL_H
#define TYPIO_RIME_INTERNAL_H

#include "typio/typio.h"
#include "typio_build_config.h"
#include "utils/log.h"
#include "utils/string.h"

#include <rime_api.h>

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef TYPIO_RIME_SHARED_DATA_DIR
#define TYPIO_RIME_SHARED_DATA_DIR "/usr/share/rime-data"
#endif

#define TYPIO_RIME_SESSION_KEY       "rime.session"
#define TYPIO_RIME_DEFAULT_SCHEMA    ""
#define TYPIO_RIME_SLOW_SYNC_MS      8

/* Modifier mask mapping from Typio modifiers to Rime masks */
enum {
    TYPIO_RIME_SHIFT_MASK    = (1 << 0),
    TYPIO_RIME_LOCK_MASK     = (1 << 1),
    TYPIO_RIME_CONTROL_MASK  = (1 << 2),
    TYPIO_RIME_MOD1_MASK     = (1 << 3),
    TYPIO_RIME_MOD2_MASK     = (1 << 4),
    TYPIO_RIME_MOD4_MASK     = (1 << 6),
    TYPIO_RIME_RELEASE_MASK  = (1 << 30),
};

/* -------------------------------------------------------------------------- */
/* Data structures                                                            */
/* -------------------------------------------------------------------------- */

typedef struct TypioRimeConfig {
    char *schema;
    char *shared_data_dir;
    char *user_data_dir;
} TypioRimeConfig;

/*
 * Notification state tracks async librime events (deploy, option change).
 * Instead of polling is_maintenance_mode() we use set_notification_handler
 * and react to callbacks.
 */
typedef struct TypioRimeState {
    RimeApi *api;
    RimeTraits traits;
    TypioRimeConfig config;
    bool initialized;
    bool maintenance_done;
    uint32_t deploy_id;
    /* Cached from the last notification callback */
    bool ascii_mode;
    bool ascii_mode_known;
} TypioRimeState;

typedef struct TypioRimeSession {
    TypioRimeState *state;
    RimeSessionId session_id;
    bool ascii_mode_known;
    bool ascii_mode;
    uint32_t deploy_id;
} TypioRimeSession;

/* -------------------------------------------------------------------------- */
/* Utility functions (rime_utils.c)                                           */
/* -------------------------------------------------------------------------- */

uint64_t typio_rime_monotonic_ms(void);
bool typio_rime_ensure_dir(const char *path);
bool typio_rime_path_exists(const char *path);
bool typio_rime_has_yaml_suffix(const char *name);

/* -------------------------------------------------------------------------- */
/* Configuration (rime_config.c)                                              */
/* -------------------------------------------------------------------------- */

TypioResult typio_rime_load_config(TypioEngine *engine,
                                   TypioInstance *instance,
                                   TypioRimeConfig *config);
void typio_rime_free_config(TypioRimeConfig *config);

/* -------------------------------------------------------------------------- */
/* Deployment (rime_deploy.c)                                                 */
/* -------------------------------------------------------------------------- */

void typio_rime_invalidate_generated_yaml(TypioRimeState *state);
bool typio_rime_run_maintenance(TypioRimeState *state, bool full_check);
bool typio_rime_is_maintaining(TypioRimeState *state);
bool typio_rime_ensure_deployed(TypioRimeState *state);

/* -------------------------------------------------------------------------- */
/* Session management (rime_session.c)                                        */
/* -------------------------------------------------------------------------- */

void typio_rime_free_session(void *data);
bool typio_rime_apply_schema(TypioRimeSession *session);
TypioRimeSession *typio_rime_get_session(TypioEngine *engine,
                                          TypioInputContext *ctx,
                                          bool create);

/* -------------------------------------------------------------------------- */
/* Context synchronisation (rime_sync.c)                                      */
/* -------------------------------------------------------------------------- */

void typio_rime_clear_state(TypioInputContext *ctx);
bool typio_rime_flush_commit(TypioRimeSession *session,
                              TypioInputContext *ctx);
bool typio_rime_sync_context(TypioRimeSession *session,
                              TypioInputContext *ctx);

/* -------------------------------------------------------------------------- */
/* Mode management (rime_mode.c)                                              */
/* -------------------------------------------------------------------------- */

extern const TypioEngineMode typio_rime_mode_chinese;
extern const TypioEngineMode typio_rime_mode_latin;

const TypioEngineMode *typio_rime_mode_for_ascii(bool ascii_mode);
void typio_rime_notify_mode(TypioEngine *engine,
                             TypioRimeSession *session,
                             bool ascii_mode);
void typio_rime_refresh_mode(TypioEngine *engine,
                               TypioRimeSession *session);
const TypioEngineMode *typio_rime_get_mode(TypioEngine *engine,
                                            TypioInputContext *ctx);
TypioResult typio_rime_set_mode(TypioEngine *engine,
                                 TypioInputContext *ctx,
                                 const char *mode_id);

/* -------------------------------------------------------------------------- */
/* Candidate actions (librime 1.16.1+)                                        */
/* -------------------------------------------------------------------------- */

bool typio_rime_highlight_candidate(TypioRimeSession *session, size_t index);
bool typio_rime_delete_candidate(TypioRimeSession *session, size_t index);

/* -------------------------------------------------------------------------- */
/* Key handling (rime_key.c)                                                  */
/* -------------------------------------------------------------------------- */

bool typio_rime_is_shift_keysym(uint32_t keysym);
uint32_t typio_rime_modifiers_to_mask(uint32_t modifiers);

#endif /* TYPIO_RIME_INTERNAL_H */
