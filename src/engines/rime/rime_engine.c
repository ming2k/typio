/**
 * @file rime_engine.c
 * @brief Rime engine plugin entry point and TypioEngineBaseOps + TypioKeyboardEngineOps implementation
 *
 * Integration with librime 1.16.1:
 *   - Uses set_notification_handler for deploy / option events instead of
 *     polling is_maintenance_mode().
 *   - Removes defensive NULL checks on api function pointers (librime 1.0+
 *     guarantees their existence after setup()).
 *   - Logs the linked librime version on init for troubleshooting.
 */

#include "rime_internal.h"

/* -------------------------------------------------------------------------- */
/* librime notification handler                                               */
/* -------------------------------------------------------------------------- */

static void typio_rime_notification(void *context_object,
                                     RimeSessionId session_id,
                                     const char *message_type,
                                     const char *message_value) {
    TypioRimeState *state = context_object;
    (void)session_id;

    if (!state || !message_type || !message_value) {
        return;
    }

    if (strcmp(message_type, "deploy") == 0) {
        if (strcmp(message_value, "success") == 0) {
            state->maintenance_done = true;
            typio_log_info("Rime deployment finished");
        } else if (strcmp(message_value, "failure") == 0) {
            typio_log_error("Rime deployment failed");
        }
        return;
    }

    if (strcmp(message_type, "option") == 0) {
        bool ascii_mode = false;
        if (strcmp(message_value, "ascii_mode") == 0) {
            ascii_mode = true;
        } else if (strcmp(message_value, "!ascii_mode") == 0) {
            ascii_mode = false;
        } else {
            return;
        }
        state->ascii_mode = ascii_mode;
        state->ascii_mode_known = true;
        return;
    }
}

/* -------------------------------------------------------------------------- */
/* TypioEngineBaseOps + TypioKeyboardEngineOps implementation                 */
/* -------------------------------------------------------------------------- */

static TypioResult typio_rime_init(TypioEngine *engine, TypioInstance *instance) {
    TypioRimeState *state;
    TypioResult result;
    const char *rime_version;

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

    /* Log linked librime version for troubleshooting */
    rime_version = state->api->get_version();
    typio_log_info("librime version: %s", rime_version ? rime_version : "unknown");

    memset(&state->traits, 0, sizeof(state->traits));
    RIME_STRUCT_INIT(RimeTraits, state->traits);
    state->traits.shared_data_dir = state->config.shared_data_dir;
    state->traits.user_data_dir = state->config.user_data_dir;
    state->traits.distribution_name = "Typio";
    state->traits.distribution_code_name = "typio";
    state->traits.distribution_version = TYPIO_VERSION;
    state->traits.app_name = "rime.typio";
    state->traits.min_log_level = 1;

    /* Install notification handler before setup so we catch deploy events */
    state->api->set_notification_handler(typio_rime_notification, state);

    state->api->setup(&state->traits);
    state->api->initialize(&state->traits);

    state->initialized = true;

    /* Kick off deployment if needed */
    typio_rime_ensure_deployed(state);

    typio_engine_set_user_data(engine, state);
    return TYPIO_OK;
}

static void typio_rime_destroy(TypioEngine *engine) {
    TypioRimeState *state = typio_engine_get_user_data(engine);

    if (!state) {
        return;
    }

    if (state->initialized) {
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

    session->state->api->clear_composition(session->session_id);
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

    if (!engine || !ctx || !event) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    is_release = (event->type == TYPIO_EVENT_KEY_RELEASE);
    is_shift = typio_rime_is_shift_keysym(event->keysym);

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

    rime_mask = typio_rime_modifiers_to_mask(event->modifiers);
    if (is_release) {
        rime_mask |= TYPIO_RIME_RELEASE_MASK;
    }

    handled = session->state->api->process_key(
        session->session_id,
        (int)event->keysym,
        (int)rime_mask);

    /* Mode switch detection via notification handler is preferred, but
     * fall back to polling get_option on Shift for older librime or
     * when the notification fires before our handler is registered. */
    if (is_shift) {
        Bool ascii_after = session->state->api->get_option(session->session_id, "ascii_mode");
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

    /* Use create=true: after a deploy, the deploy_id mismatch causes
     * get_session to recreate it fresh with the new compiled data.
     * For a plain config reload there is no deploy_id change and the existing
     * valid session is returned as before. */
    session = typio_rime_get_session(engine, ctx, true);
    if (!session) {
        return;
    }

    session->state->api->clear_composition(session->session_id);

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
        /* librime tracks source changes with second-resolution timestamps.
         * A user can rewrite default.custom.yaml twice within one second,
         * so explicit deploy must invalidate generated YAML to force rebuild. */
        typio_rime_invalidate_generated_yaml(state);
        if (!typio_rime_run_maintenance(state, true)) {
            return TYPIO_ERROR;
        }
        /* Sessions will be invalidated by deploy_id mismatch on next use */
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

/* -------------------------------------------------------------------------- */
/* Engine metadata and entry points                                           */
/* -------------------------------------------------------------------------- */

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
    .struct_size = TYPIO_ENGINE_INFO_SIZE,
};

static const TypioEngineBaseOps typio_rime_base_ops = {
    .init = typio_rime_init,
    .destroy = typio_rime_destroy,
    .focus_in = typio_rime_focus_in,
    .focus_out = typio_rime_focus_out,
    .reset = typio_rime_reset,
    .reload_config = typio_rime_reload_config,
};

static const TypioKeyboardEngineOps typio_rime_keyboard_ops = {
    .process_key = typio_rime_process_key,
    .get_mode = typio_rime_get_mode,
    .set_mode = typio_rime_set_mode,
};

const TypioEngineInfo *typio_engine_get_info(void) {
    return &typio_rime_engine_info;
}

TypioEngine *typio_engine_create(void) {
    return typio_engine_new(&typio_rime_engine_info, &typio_rime_base_ops,
                            &typio_rime_keyboard_ops, nullptr);
}
