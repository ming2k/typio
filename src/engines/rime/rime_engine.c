
#include "path_expand.h"
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

/*
 * Rime session is owned by the input context rather than the transient focus
 * state. Losing focus should clear composition UI, but the session itself
 * lives until the context is destroyed so runtime options like ascii_mode can
 * survive focus churn and engine switches within the same context.
 */
#define TYPIO_RIME_SESSION_KEY "rime.session"
#define TYPIO_RIME_DEFAULT_SCHEMA ""
#define TYPIO_RIME_SLOW_SYNC_MS 8
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
} TypioRimeConfig;

typedef struct TypioRimeState {
    RimeApi *api;
    RimeTraits traits;
    TypioRimeConfig config;
    bool initialized;
    bool maintenance_done;
    uint32_t deploy_id;
} TypioRimeState;

typedef struct TypioRimeSession {
    TypioRimeState *state;
    RimeSessionId session_id;
    bool ascii_mode_known;
    bool ascii_mode;
    uint32_t deploy_id;
} TypioRimeSession;

static const TypioEngineMode typio_rime_mode_chinese = {
    .mode_class = TYPIO_MODE_CLASS_NATIVE,
    .mode_id = "chinese",
    .display_label = "中",
    .icon_name = "typio-rime",
};

static const TypioEngineMode typio_rime_mode_latin = {
    .mode_class = TYPIO_MODE_CLASS_LATIN,
    .mode_id = "ascii",
    .display_label = "A",
    .icon_name = "typio-rime-latin",
};

static const TypioEngineMode *typio_rime_mode_for_ascii(bool ascii_mode) {
    return ascii_mode ? &typio_rime_mode_latin : &typio_rime_mode_chinese;
}

static void typio_rime_notify_mode(TypioEngine *engine,
                                   TypioRimeSession *session,
                                   bool ascii_mode) {
    if (!engine || !engine->instance || !session) {
        return;
    }

    session->ascii_mode_known = true;
    session->ascii_mode = ascii_mode;
    typio_instance_notify_mode(engine->instance, typio_rime_mode_for_ascii(ascii_mode));
}

static void typio_rime_refresh_mode(TypioEngine *engine,
                                    TypioRimeSession *session) {
    Bool ascii;

    if (!engine || !session || !session->state || !session->state->api ||
        !session->state->api->get_option) {
        return;
    }

    ascii = session->state->api->get_option(session->session_id, "ascii_mode");
    typio_rime_notify_mode(engine, session, ascii ? true : false);
}

static uint64_t typio_rime_monotonic_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

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

static bool typio_rime_has_yaml_suffix(const char *name) {
    const char *suffix;

    if (!name) {
        return false;
    }

    suffix = strrchr(name, '.');
    return suffix && strcmp(suffix, ".yaml") == 0;
}

static void typio_rime_invalidate_generated_yaml(TypioRimeState *state) {
    char *build_dir;
    DIR *dir;
    struct dirent *entry;

    if (!state || !state->config.user_data_dir) {
        return;
    }

    build_dir = typio_path_join(state->config.user_data_dir, "build");
    if (!build_dir) {
        return;
    }

    dir = opendir(build_dir);
    if (!dir) {
        free(build_dir);
        return;
    }

    while ((entry = readdir(dir)) != nullptr) {
        char *path;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (!typio_rime_has_yaml_suffix(entry->d_name)) {
            continue;
        }

        path = typio_path_join(build_dir, entry->d_name);
        if (!path) {
            continue;
        }

        if (unlink(path) != 0 && errno != ENOENT) {
            typio_log_warning("Failed to invalidate generated Rime YAML: %s", path);
        }
        free(path);
    }

    closedir(dir);
    free(build_dir);
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

    const char *sync_env = getenv("TYPIO_RIME_SYNC_DEPLOY");
    bool sync = sync_env && strcmp(sync_env, "1") == 0;

    typio_log_info("Rime deployment started (%s)", sync ? "blocking" : "non-blocking");

    state->deploy_id++;

    if (!state->api->start_maintenance(full_check ? True : False)) {
        typio_log_error("Rime deployment failed to start");
        return false;
    }

    if (sync && state->api->join_maintenance_thread) {
        state->api->join_maintenance_thread();
        state->maintenance_done = true;
    } else {
        state->maintenance_done = false;
    }

    return true;
}

static bool typio_rime_is_maintaining(TypioRimeState *state) {
    if (!state || !state->api || !state->api->is_maintenance_mode) {
        return false;
    }
    return state->api->is_maintenance_mode() ? true : false;
}

static bool typio_rime_ensure_deployed(TypioRimeState *state) {
    char *build_path;
    bool need_maintenance;

    if (!state) {
        return false;
    }

    if (state->maintenance_done) {
        return true;
    }

    /* Check if maintenance is currently running */
    if (typio_rime_is_maintaining(state)) {
        return false;
    }

    /* 
     * If we were maintaining but is_maintenance_mode() is now false,
     * it means we just finished. 
     */
    if (state->api->is_maintenance_mode && !state->api->is_maintenance_mode()) {
        /*
         * But we only want to mark it done if we actually needed it or
         * if we've already checked the build path.
         */
    }

    build_path = typio_path_join(state->config.user_data_dir, "build/default.yaml");
    need_maintenance = !typio_rime_path_exists(build_path);
    free(build_path);

    if (need_maintenance) {
        if (typio_rime_run_maintenance(state, true)) {
            /* If synchronous, it's already done */
            return state->maintenance_done;
        }
        return false;
    }

    /* No maintenance needed or it's already finished */
    state->maintenance_done = true;
    return true;
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
        if (session->deploy_id == state->deploy_id) {
            return session;
        }

        /* 
         * Deployment happened; current session is stale. Clear it from the
         * context so we create a new one below.
         */
        typio_input_context_set_property(ctx, TYPIO_RIME_SESSION_KEY, NULL, NULL);
        session = NULL;
    }

    if (!create) {
        return nullptr;
    }

    if (!typio_rime_ensure_deployed(state)) {
        /* Still deploying; don't block, just return null for now */
        return nullptr;
    }

    session = calloc(1, sizeof(*session));
    if (!session) {
        return nullptr;
    }

    session->state = state;
    session->deploy_id = state->deploy_id;
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
    typio_rime_refresh_mode(engine, session);
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

/**
 * Check whether the Rime context differs from the current InputContext only
 * in the highlighted candidate index.  When true, the caller can skip the
 * expensive full-copy path and just update the selection.
 */
static bool typio_rime_is_selection_only_change(const RimeContext *rime_context,
                                                 TypioInputContext *ctx) {
    const TypioCandidateList *current = typio_input_context_get_candidates(ctx);

    if (!current || current->count == 0) {
        return false;
    }

    if (rime_context->menu.num_candidates <= 0 || !rime_context->menu.candidates) {
        return false;
    }

    if ((size_t)rime_context->menu.num_candidates != current->count ||
        rime_context->menu.page_no != current->page ||
        rime_context->menu.page_size != current->page_size ||
        rime_context->menu.is_last_page != !current->has_next) {
        return false;
    }

    if (rime_context->menu.highlighted_candidate_index == current->selected) {
        return false;
    }

    /* Verify candidate texts actually match.  This is O(n) string comparisons
     * but n is the page size (typically 5–10) and strcmp short-circuits. */
    const size_t select_keys_len = rime_context->menu.select_keys ? strlen(rime_context->menu.select_keys) : 0;
    static const char *const fast_labels[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};

    for (int i = 0; i < rime_context->menu.num_candidates; ++i) {
        const char *rime_text = rime_context->menu.candidates[i].text;
        const char *cur_text = current->candidates[i].text;
        const char *rime_comment = rime_context->menu.candidates[i].comment;
        const char *cur_comment = current->candidates[i].comment;
        const char *rime_label = nullptr;
        const char *cur_label = current->candidates[i].label;
        char fallback_label[16];

        if (rime_context->select_labels && rime_context->select_labels[i]) {
            rime_label = rime_context->select_labels[i];
        } else if (rime_context->menu.select_keys && (size_t)i < select_keys_len) {
            fallback_label[0] = rime_context->menu.select_keys[i];
            fallback_label[1] = '\0';
            rime_label = fallback_label;
        } else if (i < 9) {
            rime_label = fast_labels[i];
        } else {
            snprintf(fallback_label, sizeof(fallback_label), "%d", i + 1);
            rime_label = fallback_label;
        }

        if (!rime_text || !cur_text || strcmp(rime_text, cur_text) != 0 ||
            strcmp(rime_comment ? rime_comment : "",
                   cur_comment ? cur_comment : "") != 0 ||
            strcmp(rime_label ? rime_label : "",
                   cur_label ? cur_label : "") != 0) {
            return false;
        }
    }

    return true;
}

static bool typio_rime_preedit_matches_context(const RimeContext *rime_context,
                                               TypioInputContext *ctx) {
    const TypioPreedit *current = typio_input_context_get_preedit(ctx);
    const char *rime_preedit;

    if (!rime_context || !ctx) {
        return false;
    }

    rime_preedit = rime_context->composition.preedit;
    if (!rime_preedit || !*rime_preedit) {
        return !current || current->segment_count == 0;
    }

    if (!current || current->segment_count != 1 || !current->segments[0].text) {
        return false;
    }

    return current->cursor_pos == rime_context->composition.cursor_pos &&
           strcmp(current->segments[0].text, rime_preedit) == 0;
}

static bool typio_rime_sync_context(TypioRimeSession *session,
                                    TypioInputContext *ctx) {
    RIME_STRUCT(RimeContext, rime_context);
    TypioRimeState *state;
    bool has_preedit = false;
    bool has_candidates = false;
    uint64_t start_ms;
    uint64_t end_ms;
    size_t candidate_count = 0;
    int selected = -1;
    int page = 0;
    int total = 0;

    if (!session || !ctx || !session->state || !session->state->api) {
        return false;
    }

    start_ms = typio_rime_monotonic_ms();
    state = session->state;
    if (!state->api->get_context ||
        !state->api->get_context(session->session_id, &rime_context)) {
        typio_rime_clear_state(ctx);
        return false;
    }

    /* Fast path: when preedit text is unchanged and only the highlighted
     * candidate moved, skip both preedit rebuilding and candidate copying. */
    if (typio_rime_preedit_matches_context(&rime_context, ctx) &&
        typio_rime_is_selection_only_change(&rime_context, ctx)) {
        selected = rime_context.menu.highlighted_candidate_index;
        typio_input_context_set_candidate_selection(ctx, selected);
        has_preedit = true;
        has_candidates = true;

        if (state->api->free_context) {
            state->api->free_context(&rime_context);
        }
        return true;
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
        TypioCandidate stack_items[10];
        char *stack_labels[10];
        TypioCandidate *items = count <= 10 ? stack_items : calloc((size_t)count, sizeof(*items));
        char **labels = count <= 10 ? stack_labels : calloc((size_t)count, sizeof(*labels));

        if (count <= 10) {
            memset(items, 0, sizeof(TypioCandidate) * (size_t)count);
            memset(labels, 0, sizeof(char *) * (size_t)count);
        }

        candidate_count = (size_t)count;
        selected = rime_context.menu.highlighted_candidate_index;
        page = rime_context.menu.page_no;
        total = rime_context.menu.page_no * rime_context.menu.page_size +
                count + (rime_context.menu.is_last_page ? 0 :
                         rime_context.menu.page_size);

        if (items && labels) {
            TypioCandidateList list = {
                .candidates = items,
                .count = (size_t)count,
                .page = rime_context.menu.page_no,
                .page_size = rime_context.menu.page_size,
                .total = total,
                .selected = rime_context.menu.highlighted_candidate_index,
                .has_prev = rime_context.menu.page_no > 0,
                .has_next = !rime_context.menu.is_last_page,
            };

            const size_t select_keys_len = rime_context.menu.select_keys ? strlen(rime_context.menu.select_keys) : 0;
            static const char *const fast_labels[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};

            for (int i = 0; i < count; ++i) {
                items[i].text = rime_context.menu.candidates[i].text;
                items[i].comment = rime_context.menu.candidates[i].comment;

                if (rime_context.select_labels && rime_context.select_labels[i]) {
                    items[i].label = rime_context.select_labels[i];
                    continue;
                }

                if (rime_context.menu.select_keys && (size_t)i < select_keys_len) {
                    char label[2] = {rime_context.menu.select_keys[i], '\0'};
                    labels[i] = typio_strdup(label);
                    items[i].label = labels[i];
                    continue;
                }

                if (i < 9) {
                    items[i].label = fast_labels[i];
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
            if (count > 10) free(labels);
        }
        if (count > 10) free(items);
    } else {
        typio_input_context_clear_candidates(ctx);
    }

    if (state->api->free_context) {
        state->api->free_context(&rime_context);
    }

    end_ms = typio_rime_monotonic_ms();
    if (end_ms >= start_ms && (end_ms - start_ms) >= TYPIO_RIME_SLOW_SYNC_MS) {
        typio_log_debug(
            "Rime sync slow: total=%" PRIu64 "ms session=%u candidates=%zu selected=%d page=%d total_candidates=%d preedit=%s",
            end_ms - start_ms,
            (unsigned int)session->session_id,
            candidate_count,
            selected,
            page,
            total,
            has_preedit ? "yes" : "no");
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

    /* 
     * Kick off deployment if needed.  In non-blocking mode, ensure_deployed
     * returns false while maintenance is running.  Initialization should
     * still succeed; the engine will simply stay in "deploying" state
     * (returning HANDLED with a preedit message) until it finishes.
     */
    typio_rime_ensure_deployed(state);

    typio_engine_set_user_data(engine, state);
    return TYPIO_OK;
}

static void typio_rime_destroy(TypioEngine *engine) {
    TypioRimeState *state = typio_engine_get_user_data(engine);

    if (!state) {
        return;
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
        if (!session->ascii_mode_known) {
            typio_rime_refresh_mode(engine, session);
        }
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
    if (session->ascii_mode_known) {
        typio_rime_notify_mode(engine, session, session->ascii_mode);
    } else {
        typio_rime_refresh_mode(engine, session);
    }
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
    int ascii_after;

    if (!engine || !ctx || !event) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    is_release = (event->type == TYPIO_EVENT_KEY_RELEASE);
    is_shift = typio_rime_is_shift_keysym(event->keysym);
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
    if (!session) {
        if (typio_rime_is_maintaining(typio_engine_get_user_data(engine))) {
            /* Show a temporary preedit message during deployment */
            TypioPreeditSegment segment = {
                .text = "… Rime 正在部署",
                .format = TYPIO_PREEDIT_NONE,
            };
            TypioPreedit preedit = {
                .segments = &segment,
                .segment_count = 1,
                .cursor_pos = 0,
            };
            typio_input_context_set_preedit(ctx, &preedit);
            return TYPIO_KEY_HANDLED;
        }
        return TYPIO_KEY_NOT_HANDLED;
    }

    if (!session->state->api->process_key) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    rime_mask = typio_rime_modifiers_to_mask(event->modifiers);
    if (is_release) {
        rime_mask |= TYPIO_RIME_RELEASE_MASK;
    }

    handled = session->state->api->process_key(
        session->session_id,
        (int)event->keysym,
        (int)rime_mask);

    if (is_shift && session->state->api->get_option) {
        ascii_after = session->state->api->get_option(session->session_id, "ascii_mode");
    }

    if (is_shift && ascii_after >= 0) {
        typio_rime_notify_mode(engine, session, ascii_after != 0);
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

    /*
     * Use create=true: after a deploy, the deploy_id mismatch causes
     * get_session to recreate it fresh with the new compiled data.
     * For a plain config reload there is no deploy_id change and the existing
     * valid session is returned as before.
     */
    session = typio_rime_get_session(engine, ctx, true);
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

    if (typio_instance_rime_deploy_requested(engine->instance)) {
        /*
         * librime tracks source changes with second-resolution timestamps.
         * A user can rewrite default.custom.yaml twice within one second,
         * so explicit deploy must invalidate generated YAML to force rebuild.
         */
        typio_rime_invalidate_generated_yaml(state);
        if (!typio_rime_run_maintenance(state, true)) {
            return TYPIO_ERROR;
        }
        /*
         * Invalidate all existing sessions so they are recreated with the
         * newly compiled Rime data.  Sessions on unfocused contexts will be
         * recreated on their next focus_in; the focused context session is
         * recreated below by apply_runtime_config (which uses create=true).
         */
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

static const TypioEngineMode *typio_rime_get_mode(TypioEngine *engine,
                                                   TypioInputContext *ctx) {
    TypioRimeSession *session = typio_rime_get_session(engine, ctx, false);

    if (!session || !session->ascii_mode_known) {
        return &typio_rime_mode_chinese;
    }

    return typio_rime_mode_for_ascii(session->ascii_mode);
}

static TypioResult typio_rime_set_mode(TypioEngine *engine,
                                       TypioInputContext *ctx,
                                       const char *mode_id) {
    TypioRimeSession *session;
    Bool ascii_mode;

    if (!engine || !ctx || !mode_id || !*mode_id) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    session = typio_rime_get_session(engine, ctx, true);
    if (!session || !session->state || !session->state->api ||
        !session->state->api->set_option) {
        return TYPIO_ERROR_NOT_INITIALIZED;
    }

    if (strcmp(mode_id, "ascii") == 0) {
        ascii_mode = True;
    } else if (strcmp(mode_id, "chinese") == 0) {
        ascii_mode = False;
    } else {
        return TYPIO_ERROR_NOT_FOUND;
    }

    session->state->api->set_option(session->session_id, "ascii_mode", ascii_mode);
    typio_rime_notify_mode(engine, session, ascii_mode ? true : false);
    return TYPIO_OK;
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
    .reload_config = typio_rime_reload_config,
    .get_mode = typio_rime_get_mode,
    .set_mode = typio_rime_set_mode,
};

const TypioEngineInfo *typio_engine_get_info(void) {
    return &typio_rime_engine_info;
}

TypioEngine *typio_engine_create(void) {
    return typio_engine_new(&typio_rime_engine_info, &typio_rime_engine_ops);
}
