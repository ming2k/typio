
#include "path_expand.h"
#include "typio/typio.h"
#include "typio_build_config.h"
#include "utils/log.h"
#include "utils/string.h"

#include <rime_api.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef TYPIO_RIME_SHARED_DATA_DIR
#define TYPIO_RIME_SHARED_DATA_DIR "/usr/share/rime-data"
#endif

/*
 * Rime session is owned by the input context rather than the transient focus
 * state. Losing focus should clear composition UI, but the session itself
 * lives until the context is destroyed so runtime options like ascii_mode can
 * survive focus churn and engine switches within the same context.
 */
#define TYPIO_RIME_SESSION_KEY "rime.session"
#define TYPIO_RIME_DEFAULT_SCHEMA ""
enum {
    TYPIO_RIME_SHIFT_MASK = (1 << 0),
    TYPIO_RIME_LOCK_MASK = (1 << 1),
    TYPIO_RIME_CONTROL_MASK = (1 << 2),
    TYPIO_RIME_MOD1_MASK = (1 << 3),
    TYPIO_RIME_MOD2_MASK = (1 << 4),
    TYPIO_RIME_MOD4_MASK = (1 << 6),
    TYPIO_RIME_RELEASE_MASK = (1 << 30),
};

typedef struct TypioRimeConfig {
    char *schema;
    char *shared_data_dir;
    char *user_data_dir;
    bool full_check;
} TypioRimeConfig;

typedef struct TypioRimeState {
    RimeApi *api;
    RimeTraits traits;
    TypioRimeConfig config;
    bool initialized;
    bool maintenance_done;
} TypioRimeState;

typedef struct TypioRimeSession {
    TypioRimeState *state;
    RimeSessionId session_id;
} TypioRimeSession;

static bool typio_rime_is_shift_keysym(uint32_t keysym) {
    return keysym == TYPIO_KEY_Shift_L || keysym == TYPIO_KEY_Shift_R;
}

static void typio_rime_free_config(TypioRimeConfig *config) {
    if (!config) {
        return;
    }

    free(config->schema);
    free(config->shared_data_dir);
    free(config->user_data_dir);
    memset(config, 0, sizeof(*config));
}

static bool typio_rime_ensure_dir(const char *path) {
    if (!path || !*path) {
        return false;
    }

    char *copy = typio_strdup(path);
    if (!copy) {
        return false;
    }

    for (char *p = copy + 1; *p; ++p) {
        if (*p != '/') {
            continue;
        }

        *p = '\0';
        if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
            free(copy);
            return false;
        }
        *p = '/';
    }

    const bool ok = mkdir(copy, 0755) == 0 || errno == EEXIST;
    free(copy);
    return ok;
}

static bool typio_rime_path_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

static TypioResult typio_rime_load_config(TypioEngine *engine,
                                          TypioInstance *instance,
                                          TypioRimeConfig *config) {
    char *persisted_schema;
    const char *data_dir;
    TypioConfig *engine_config = nullptr;
    char *default_user_dir;

    if (!engine || !instance || !config) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    memset(config, 0, sizeof(*config));
    config->schema = typio_strdup(TYPIO_RIME_DEFAULT_SCHEMA);
    config->shared_data_dir = typio_strdup(TYPIO_RIME_SHARED_DATA_DIR);
    data_dir = typio_instance_get_data_dir(instance);
    default_user_dir = typio_path_join(data_dir, "rime");
    config->user_data_dir = default_user_dir;

    if (!config->schema || !config->shared_data_dir || !config->user_data_dir) {
        typio_rime_free_config(config);
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    persisted_schema = typio_instance_dup_rime_schema(instance);
    if (persisted_schema && *persisted_schema) {
        free(config->schema);
        config->schema = persisted_schema;
        persisted_schema = nullptr;
    }
    engine_config = typio_instance_get_engine_config(instance, "rime");
    if (!engine_config) {
        return TYPIO_OK;
    }

    const char *shared_data_dir = typio_config_get_string(engine_config, "shared_data_dir", nullptr);
    const char *user_data_dir = typio_config_get_string(engine_config, "user_data_dir", nullptr);
    const bool full_check = typio_config_get_bool(engine_config, "full_check", false);
    if (shared_data_dir && *shared_data_dir) {
        char *expanded = typio_rime_expand_path(shared_data_dir);
        free(config->shared_data_dir);
        config->shared_data_dir = expanded;
    }
    if (user_data_dir && *user_data_dir) {
        char *expanded = typio_rime_expand_path(user_data_dir);
        free(config->user_data_dir);
        config->user_data_dir = expanded;
    }
    config->full_check = full_check;

    typio_config_free(engine_config);
    free(persisted_schema);

    if (!config->schema || !config->shared_data_dir || !config->user_data_dir) {
        typio_rime_free_config(config);
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    return TYPIO_OK;
}

static bool typio_rime_run_maintenance(TypioRimeState *state, bool full_check) {
    if (!state || !state->api || !state->api->start_maintenance) {
        return false;
    }

    typio_log_info("Rime deployment started");

    if (!state->api->start_maintenance(full_check ? True : False)) {
        typio_log_error("Rime deployment failed to start");
        return false;
    }

    if (state->api->join_maintenance_thread) {
        state->api->join_maintenance_thread();
    }

    state->maintenance_done = true;
    return true;
}

static bool typio_rime_ensure_deployed(TypioRimeState *state) {
    char *build_path;
    bool need_maintenance;
    bool ok = true;

    if (!state) {
        return false;
    }

    if (state->maintenance_done) {
        return true;
    }

    build_path = typio_path_join(state->config.user_data_dir, "build/default.yaml");
    need_maintenance = !typio_rime_path_exists(build_path);
    free(build_path);

    if (state->config.full_check) {
        need_maintenance = true;
    }

    if (need_maintenance) {
        ok = typio_rime_run_maintenance(state, true);
    }

    return ok;
}

static void typio_rime_free_session(void *data) {
    TypioRimeSession *session = data;

    if (!session) {
        return;
    }

    if (session->state &&
        session->state->api &&
        session->state->api->destroy_session &&
        session->session_id != 0 &&
        (!session->state->api->find_session ||
         session->state->api->find_session(session->session_id))) {
        session->state->api->destroy_session(session->session_id);
    }

    free(session);
}

static uint32_t typio_rime_modifiers_to_mask(uint32_t modifiers) {
    uint32_t mask = 0;

    if (modifiers & TYPIO_MOD_SHIFT) {
        mask |= TYPIO_RIME_SHIFT_MASK;
    }
    if (modifiers & TYPIO_MOD_CTRL) {
        mask |= TYPIO_RIME_CONTROL_MASK;
    }
    if (modifiers & TYPIO_MOD_ALT) {
        mask |= TYPIO_RIME_MOD1_MASK;
    }
    if (modifiers & TYPIO_MOD_SUPER) {
        mask |= TYPIO_RIME_MOD4_MASK;
    }
    if (modifiers & TYPIO_MOD_CAPSLOCK) {
        mask |= TYPIO_RIME_LOCK_MASK;
    }
    if (modifiers & TYPIO_MOD_NUMLOCK) {
        mask |= TYPIO_RIME_MOD2_MASK;
    }

    return mask;
}

static void typio_rime_clear_state(TypioInputContext *ctx) {
    typio_input_context_clear_preedit(ctx);
    typio_input_context_clear_candidates(ctx);
}

static bool typio_rime_apply_schema(TypioRimeSession *session) {
    TypioRimeState *state;
    bool ok;

    if (!session || !session->state || !session->state->api) {
        return false;
    }

    state = session->state;
    if (!state->config.schema || !*state->config.schema) {
        return true;
    }

    ok = state->api->select_schema &&
         state->api->select_schema(session->session_id, state->config.schema);
    if (!ok && !state->maintenance_done) {
        if (!typio_rime_ensure_deployed(state)) {
            return false;
        }
        ok = state->api->select_schema &&
             state->api->select_schema(session->session_id, state->config.schema);
    }

    if (!ok) {
        typio_log_warning("Failed to select Rime schema: %s", state->config.schema);
    }

    return ok;
}

static TypioRimeSession *typio_rime_get_session(TypioEngine *engine,
                                                TypioInputContext *ctx,
                                                bool create) {
    TypioRimeState *state = typio_engine_get_user_data(engine);
    TypioRimeSession *session;

    if (!state || !state->api) {
        return nullptr;
    }

    session = typio_input_context_get_property(ctx, TYPIO_RIME_SESSION_KEY);
    if (session &&
        (!state->api->find_session || state->api->find_session(session->session_id))) {
        return session;
    }

    if (!create) {
        return nullptr;
    }

    if (!typio_rime_ensure_deployed(state)) {
        return nullptr;
    }

    session = calloc(1, sizeof(*session));
    if (!session) {
        return nullptr;
    }

    session->state = state;
    session->session_id = state->api->create_session ? state->api->create_session() : 0;
    if (session->session_id == 0) {
        free(session);
        typio_log_error("Failed to create Rime session");
        return nullptr;
    }

    if (!typio_rime_apply_schema(session)) {
        typio_rime_free_session(session);
        return nullptr;
    }

    typio_input_context_set_property(ctx, TYPIO_RIME_SESSION_KEY, session, typio_rime_free_session);
    return session;
}

static bool typio_rime_flush_commit(TypioRimeSession *session,
                                    TypioInputContext *ctx) {
    RIME_STRUCT(RimeCommit, commit);
    bool committed = false;

    if (!session || !ctx || !session->state || !session->state->api) {
        return false;
    }

    if (session->state->api->get_commit &&
        session->state->api->get_commit(session->session_id, &commit)) {
        if (commit.text && *commit.text) {
            typio_input_context_commit(ctx, commit.text);
            committed = true;
        }
        if (session->state->api->free_commit) {
            session->state->api->free_commit(&commit);
        }
    }

    return committed;
}

static bool typio_rime_sync_context(TypioRimeSession *session,
                                    TypioInputContext *ctx) {
    RIME_STRUCT(RimeContext, rime_context);
    TypioRimeState *state;
    bool has_preedit = false;
    bool has_candidates = false;

    if (!session || !ctx || !session->state || !session->state->api) {
        return false;
    }

    state = session->state;
    if (!state->api->get_context ||
        !state->api->get_context(session->session_id, &rime_context)) {
        typio_rime_clear_state(ctx);
        return false;
    }

    if (rime_context.composition.preedit && *rime_context.composition.preedit) {
        TypioPreeditSegment segment = {
            .text = rime_context.composition.preedit,
            .format = TYPIO_PREEDIT_UNDERLINE,
        };
        TypioPreedit preedit = {
            .segments = &segment,
            .segment_count = 1,
            .cursor_pos = rime_context.composition.cursor_pos,
        };
        typio_input_context_set_preedit(ctx, &preedit);
        has_preedit = true;
    } else {
        typio_input_context_clear_preedit(ctx);
    }

    if (rime_context.menu.num_candidates > 0 && rime_context.menu.candidates) {
        const int count = rime_context.menu.num_candidates;
        TypioCandidate *items = calloc((size_t)count, sizeof(*items));
        char **labels = calloc((size_t)count, sizeof(*labels));

        if (items && labels) {
            TypioCandidateList list = {
                .candidates = items,
                .count = (size_t)count,
                .page = rime_context.menu.page_no,
                .page_size = rime_context.menu.page_size,
                .total = rime_context.menu.page_no * rime_context.menu.page_size +
                         count + (rime_context.menu.is_last_page ? 0 :
                                  rime_context.menu.page_size),
                .selected = rime_context.menu.highlighted_candidate_index,
                .has_prev = rime_context.menu.page_no > 0,
                .has_next = !rime_context.menu.is_last_page,
            };

            for (int i = 0; i < count; ++i) {
                items[i].text = rime_context.menu.candidates[i].text;
                items[i].comment = rime_context.menu.candidates[i].comment;

                if (rime_context.select_labels && rime_context.select_labels[i]) {
                    items[i].label = rime_context.select_labels[i];
                    continue;
                }

                if (rime_context.menu.select_keys &&
                    (size_t)i < strlen(rime_context.menu.select_keys)) {
                    char label[2] = {rime_context.menu.select_keys[i], '\0'};
                    labels[i] = typio_strdup(label);
                    items[i].label = labels[i];
                    continue;
                }

                char label[16];
                snprintf(label, sizeof(label), "%d", i + 1);
                labels[i] = typio_strdup(label);
                items[i].label = labels[i];
            }

            typio_input_context_set_candidates(ctx, &list);
            has_candidates = true;
        } else {
            typio_input_context_clear_candidates(ctx);
        }

        if (labels) {
            for (int i = 0; i < count; ++i) {
                free(labels[i]);
            }
        }
        free(labels);
        free(items);
    } else {
        typio_input_context_clear_candidates(ctx);
    }

    if (state->api->free_context) {
        state->api->free_context(&rime_context);
    }

    return has_preedit || has_candidates;
}

static TypioResult typio_rime_init(TypioEngine *engine, TypioInstance *instance) {
    TypioRimeState *state;
    TypioResult result;

    if (!engine || !instance) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    state = calloc(1, sizeof(*state));
    if (!state) {
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    result = typio_rime_load_config(engine, instance, &state->config);
    if (result != TYPIO_OK) {
        free(state);
        return result;
    }

    if (!typio_rime_ensure_dir(state->config.user_data_dir)) {
        typio_rime_free_config(&state->config);
        free(state);
        return TYPIO_ERROR;
    }

    state->api = rime_get_api();
    if (!state->api) {
        typio_rime_free_config(&state->config);
        free(state);
        return TYPIO_ERROR;
    }

    RIME_STRUCT_INIT(RimeTraits, state->traits);
    state->traits.shared_data_dir = state->config.shared_data_dir;
    state->traits.user_data_dir = state->config.user_data_dir;
    state->traits.distribution_name = "Typio";
    state->traits.distribution_code_name = "typio";
    state->traits.distribution_version = TYPIO_VERSION;
    state->traits.app_name = "rime.typio";
    state->traits.min_log_level = 1;

    if (state->api->setup) {
        state->api->setup(&state->traits);
    }
    if (state->api->initialize) {
        state->api->initialize(&state->traits);
    }

    state->initialized = true;

    if (!typio_rime_ensure_deployed(state)) {
        if (state->api->finalize) {
            state->api->finalize();
        }
        typio_rime_free_config(&state->config);
        free(state);
        return TYPIO_ERROR;
    }

    typio_engine_set_user_data(engine, state);
    return TYPIO_OK;
}

static void typio_rime_destroy(TypioEngine *engine) {
    TypioRimeState *state = typio_engine_get_user_data(engine);

    if (!state) {
        return;
    }

    if (state->initialized && state->api && state->api->cleanup_stale_sessions) {
        state->api->cleanup_stale_sessions();
    }
    if (state->initialized && state->api && state->api->finalize) {
        state->api->finalize();
    }

    typio_rime_free_config(&state->config);
    free(state);
    typio_engine_set_user_data(engine, nullptr);
}

static void typio_rime_focus_in(TypioEngine *engine, TypioInputContext *ctx) {
    TypioRimeSession *session = typio_rime_get_session(engine, ctx, true);
    if (session) {
        typio_rime_sync_context(session, ctx);
    }
}

static void typio_rime_reset(TypioEngine *engine, TypioInputContext *ctx) {
    TypioRimeSession *session = typio_rime_get_session(engine, ctx, false);

    if (!session) {
        typio_rime_clear_state(ctx);
        return;
    }

    if (session->state->api->clear_composition) {
        session->state->api->clear_composition(session->session_id);
    }
    typio_rime_clear_state(ctx);
}

static void typio_rime_focus_out(TypioEngine *engine, TypioInputContext *ctx) {
    /* Focus loss ends composition, not the context-owned Rime session. */
    typio_rime_reset(engine, ctx);
}

static TypioKeyProcessResult typio_rime_process_key(TypioEngine *engine,
                                                    TypioInputContext *ctx,
                                                    const TypioKeyEvent *event) {
    TypioRimeSession *session;
    bool handled;
    bool committed;
    bool composing;
    bool is_release;
    bool is_shift;
    uint32_t rime_mask;
    int ascii_before;
    int ascii_after;

    if (!engine || !ctx || !event) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    is_release = (event->type == TYPIO_EVENT_KEY_RELEASE);
    is_shift = typio_rime_is_shift_keysym(event->keysym);
    ascii_before = -1;
    ascii_after = -1;

    /* Handle Escape on press only */
    if (!is_release && typio_key_event_is_escape(event)) {
        const TypioPreedit *preedit = typio_input_context_get_preedit(ctx);
        const TypioCandidateList *candidates = typio_input_context_get_candidates(ctx);

        if ((preedit && preedit->segment_count > 0) ||
            (candidates && candidates->count > 0)) {
            typio_rime_reset(engine, ctx);
            return TYPIO_KEY_HANDLED;
        }

        return TYPIO_KEY_NOT_HANDLED;
    }

    session = typio_rime_get_session(engine, ctx, true);
    if (!session || !session->state->api->process_key) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    rime_mask = typio_rime_modifiers_to_mask(event->modifiers);
    if (is_release) {
        rime_mask |= TYPIO_RIME_RELEASE_MASK;
    }

    if (is_shift && session->state->api->get_option) {
        ascii_before = session->state->api->get_option(session->session_id, "ascii_mode");
    }

    handled = session->state->api->process_key(
        session->session_id,
        (int)event->keysym,
        (int)rime_mask);

    if (is_shift && session->state->api->get_option) {
        ascii_after = session->state->api->get_option(session->session_id, "ascii_mode");
        typio_log(TYPIO_LOG_DEBUG,
                  "Rime Shift diagnostic: state=%s handled=%s keysym=0x%x mods=0x%x mask=0x%x ascii_before=%d ascii_after=%d session=%u",
                  is_release ? "release" : "press",
                  handled ? "yes" : "no",
                  event->keysym,
                  event->modifiers,
                  rime_mask,
                  ascii_before,
                  ascii_after,
                  (unsigned int)session->session_id);
    }

    if (!handled) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    committed = typio_rime_flush_commit(session, ctx);
    composing = typio_rime_sync_context(session, ctx);

    if (committed) {
        return TYPIO_KEY_COMMITTED;
    }
    if (composing) {
        return TYPIO_KEY_COMPOSING;
    }
    return TYPIO_KEY_HANDLED;
}

static bool typio_rime_select_candidate(TypioEngine *engine,
                                        TypioInputContext *ctx,
                                        int index) {
    TypioRimeSession *session = typio_rime_get_session(engine, ctx, false);

    if (!session || index < 0) {
        return false;
    }

    if (session->state->api->select_candidate_on_current_page) {
        if (!session->state->api->select_candidate_on_current_page(
                session->session_id, (size_t)index)) {
            return false;
        }
    } else if (session->state->api->select_candidate) {
        if (!session->state->api->select_candidate(session->session_id, (size_t)index)) {
            return false;
        }
    } else {
        return false;
    }

    typio_rime_flush_commit(session, ctx);
    typio_rime_sync_context(session, ctx);
    return true;
}

static bool typio_rime_page_candidates(TypioEngine *engine,
                                       TypioInputContext *ctx,
                                       bool next) {
    TypioRimeSession *session = typio_rime_get_session(engine, ctx, false);

    if (!session || !session->state->api->change_page) {
        return false;
    }

    if (!session->state->api->change_page(session->session_id, next ? False : True)) {
        return false;
    }

    typio_rime_sync_context(session, ctx);
    return true;
}

static void typio_rime_apply_runtime_config(TypioEngine *engine) {
    TypioInputContext *ctx;
    TypioRimeSession *session;

    if (!engine || !engine->instance) {
        return;
    }

    ctx = typio_instance_get_focused_context(engine->instance);
    if (!ctx) {
        return;
    }

    session = typio_rime_get_session(engine, ctx, false);
    if (!session) {
        return;
    }

    if (session->state->api->clear_composition) {
        session->state->api->clear_composition(session->session_id);
    }

    if (!typio_rime_apply_schema(session)) {
        return;
    }

    typio_rime_sync_context(session, ctx);
}

static TypioResult typio_rime_reload_config(TypioEngine *engine) {
    TypioRimeState *state = typio_engine_get_user_data(engine);
    TypioConfig *engine_config;
    char *schema;

    if (!state) {
        return TYPIO_ERROR_NOT_INITIALIZED;
    }

    if (!engine->instance) {
        return TYPIO_OK;
    }

    schema = typio_instance_dup_rime_schema(engine->instance);
    free(state->config.schema);
    state->config.schema = schema ? schema : typio_strdup(TYPIO_RIME_DEFAULT_SCHEMA);

    engine_config = typio_instance_get_engine_config(engine->instance, "rime");
    if (!engine_config) {
        typio_rime_apply_runtime_config(engine);
        return TYPIO_OK;
    }

    if (typio_config_has_key(engine_config, "shared_data_dir") ||
        typio_config_has_key(engine_config, "user_data_dir")) {
        typio_log_warning("Rime data directories require restarting Typio to take effect");
    }

    typio_config_free(engine_config);
    typio_rime_apply_runtime_config(engine);
    return TYPIO_OK;
}

static const char *typio_rime_get_status_icon(TypioEngine *engine,
                                               TypioInputContext *ctx) {
    TypioRimeSession *session = typio_rime_get_session(engine, ctx, false);
    Bool ascii;

    if (!session || !session->state->api->get_option) {
        return "typio-rime";
    }

    ascii = session->state->api->get_option(session->session_id, "ascii_mode");
    typio_log(TYPIO_LOG_DEBUG,
              "Rime status icon diagnostic: ascii_mode=%d icon=%s session=%u",
              ascii ? 1 : 0,
              ascii ? "typio-rime-latin" : "typio-rime",
              (unsigned int)session->session_id);
    return ascii ? "typio-rime-latin" : "typio-rime";
}

static const TypioEngineInfo typio_rime_engine_info = {
    .name = "rime",
    .display_name = "Rime",
    .description = "Chinese input engine powered by librime.",
    .version = TYPIO_VERSION,
    .author = "Typio",
    .icon = "typio-rime",
    .language = "zh_CN",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
    .capabilities = TYPIO_CAP_PREEDIT | TYPIO_CAP_CANDIDATES |
                    TYPIO_CAP_PREDICTION | TYPIO_CAP_LEARNING,
    .api_version = TYPIO_API_VERSION,
};

static const TypioEngineOps typio_rime_engine_ops = {
    .init = typio_rime_init,
    .destroy = typio_rime_destroy,
    .focus_in = typio_rime_focus_in,
    .focus_out = typio_rime_focus_out,
    .reset = typio_rime_reset,
    .process_key = typio_rime_process_key,
    .select_candidate = typio_rime_select_candidate,
    .page_candidates = typio_rime_page_candidates,
    .reload_config = typio_rime_reload_config,
    .get_status_icon = typio_rime_get_status_icon,
};

const TypioEngineInfo *typio_engine_get_info(void) {
    return &typio_rime_engine_info;
}

TypioEngine *typio_engine_create(void) {
    return typio_engine_new(&typio_rime_engine_info, &typio_rime_engine_ops);
}
