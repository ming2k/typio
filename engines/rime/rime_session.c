/**
 * @file rime_session.c
 * @brief librime session lifecycle tied to TypioInputContext
 *
 * Rime sessions are owned by the input context rather than transient focus
 * state. Losing focus clears composition UI, but the session itself lives
 * until the context is destroyed so runtime options like ascii_mode survive
 * focus churn and engine switches within the same context.
 */

#include "rime_internal.h"

void typio_rime_free_session(void *data) {
    TypioRimeSession *session = data;

    if (!session) {
        return;
    }

    if (session->state &&
        session->session_id != 0 &&
        session->state->api->find_session(session->session_id)) {
        session->state->api->destroy_session(session->session_id);
    }

    free(session);
}

bool typio_rime_apply_schema(TypioRimeSession *session) {
    TypioRimeState *state;
    bool ok;

    if (!session || !session->state) {
        return false;
    }

    state = session->state;
    if (!state->config.schema || !*state->config.schema) {
        return true;
    }

    ok = state->api->select_schema(session->session_id, state->config.schema);
    if (!ok && !state->maintenance_done) {
        if (!typio_rime_ensure_deployed(state)) {
            return false;
        }
        ok = state->api->select_schema(session->session_id, state->config.schema);
    }

    if (ok) {
        RIME_STRUCT(RimeStatus, status);
        if (state->api->get_status(session->session_id, &status)) {
            if (status.schema_id &&
                strcmp(status.schema_id, state->config.schema) != 0) {
                typio_log_warning(
                    "Schema mismatch after select_schema: expected '%s', got '%s'",
                    state->config.schema, status.schema_id);
            }
            state->api->free_status(&status);
        }
    } else {
        typio_log_warning("Failed to select Rime schema: %s", state->config.schema);
    }

    return ok;
}

TypioRimeSession *typio_rime_get_session(TypioEngine *engine,
                                          TypioInputContext *ctx,
                                          bool create) {
    TypioRimeState *state = typio_engine_get_user_data(engine);
    TypioRimeSession *session;

    if (!state || !state->api) {
        return nullptr;
    }

    session = typio_input_context_get_property(ctx, TYPIO_RIME_SESSION_KEY);
    if (session && state->api->find_session(session->session_id)) {
        if (session->deploy_id == state->deploy_id) {
            return session;
        }

        /* Deployment happened; current session is stale. Clear it so we
         * create a new one below. */
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
    session->session_id = state->api->create_session();
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
