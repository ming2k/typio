/**
 * @file wl_input_method.c
 * @brief zwp_input_method_v2 event handlers
 */

#include "wl_frontend_internal.h"
#include "inline_ui.h"
#include "wl_trace.h"
#include "typio/typio.h"
#include "utils/log.h"
#include "utils/string.h"

#include <stdlib.h>
#include <string.h>

/* Forward declarations for callbacks */
static void on_commit_callback(TypioInputContext *ctx, const char *text,
                               void *user_data);
static void on_preedit_callback(TypioInputContext *ctx,
                                const TypioPreedit *preedit, void *user_data);
static void on_candidate_callback(TypioInputContext *ctx,
                                  const TypioCandidateList *candidates,
                                  void *user_data);
static void update_inline_ui(TypioWlSession *session, TypioInputContext *ctx);
static void update_wayland_text_ui(TypioWlSession *session, TypioInputContext *ctx);
static bool session_is_focused(TypioWlFrontend *frontend);
static void set_pending_reactivation(TypioWlFrontend *frontend, bool pending);
static TypioEngine *active_engine(TypioWlFrontend *frontend);
static bool rebuild_keyboard_grab(TypioWlFrontend *frontend,
                                  const char *reset_reason,
                                  const char *failure_reason);

/* Input method event handlers */
static void im_handle_activate(void *data, struct zwp_input_method_v2 *im);
static void im_handle_deactivate(void *data, struct zwp_input_method_v2 *im);
static void im_handle_surrounding_text(void *data, struct zwp_input_method_v2 *im,
                                       const char *text, uint32_t cursor,
                                       uint32_t anchor);
static void im_handle_text_change_cause(void *data, struct zwp_input_method_v2 *im,
                                        uint32_t cause);
static void im_handle_content_type(void *data, struct zwp_input_method_v2 *im,
                                   uint32_t hint, uint32_t purpose);
static void im_handle_done(void *data, struct zwp_input_method_v2 *im);
static void im_handle_unavailable(void *data, struct zwp_input_method_v2 *im);

static const struct zwp_input_method_v2_listener input_method_listener = {
    .activate = im_handle_activate,
    .deactivate = im_handle_deactivate,
    .surrounding_text = im_handle_surrounding_text,
    .text_change_cause = im_handle_text_change_cause,
    .content_type = im_handle_content_type,
    .done = im_handle_done,
    .unavailable = im_handle_unavailable,
};

void typio_wl_input_method_setup(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->input_method) {
        return;
    }
    zwp_input_method_v2_add_listener(frontend->input_method,
                                     &input_method_listener, frontend);
}

static bool session_is_focused(TypioWlFrontend *frontend) {
    return frontend && frontend->session && frontend->session->ctx &&
           typio_input_context_is_focused(frontend->session->ctx);
}

static void set_pending_reactivation(TypioWlFrontend *frontend, bool pending) {
    if (frontend) {
        frontend->pending_reactivation = pending;
    }
}

static TypioEngine *active_engine(TypioWlFrontend *frontend) {
    TypioEngineManager *manager;

    if (!frontend || !frontend->instance) {
        return NULL;
    }

    manager = typio_instance_get_engine_manager(frontend->instance);
    return manager ? typio_engine_manager_get_active(manager) : NULL;
}

static bool rebuild_keyboard_grab(TypioWlFrontend *frontend,
                                  const char *reset_reason,
                                  const char *failure_reason) {
    if (!frontend) {
        return false;
    }

    typio_wl_lifecycle_hard_reset_keyboard(frontend,
                                           reset_reason ? reset_reason : "keyboard rebuild");
    frontend->keyboard = typio_wl_keyboard_create(frontend);
    if (!frontend->keyboard) {
        typio_log(TYPIO_LOG_ERROR,
                  "%s", failure_reason ? failure_reason : "Failed to rebuild keyboard grab");
        return false;
    }

    return true;
}

/* Session management */
TypioWlSession *typio_wl_session_create(TypioWlFrontend *frontend) {
    TypioWlSession *session = calloc(1, sizeof(TypioWlSession));
    if (!session) {
        return NULL;
    }

    session->frontend = frontend;

    /* Create input context */
    session->ctx = typio_instance_create_context(frontend->instance);
    if (!session->ctx) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create input context");
        free(session);
        return NULL;
    }

    /* Set up callbacks */
    typio_input_context_set_commit_callback(session->ctx, on_commit_callback, session);
    typio_input_context_set_preedit_callback(session->ctx, on_preedit_callback, session);
    typio_input_context_set_candidate_callback(session->ctx, on_candidate_callback, session);
    typio_input_context_set_user_data(session->ctx, session);

    return session;
}

void typio_wl_session_destroy(TypioWlSession *session) {
    if (!session) {
        return;
    }

    if (session->ctx) {
        typio_input_context_focus_out(session->ctx);
        typio_instance_destroy_context(session->frontend->instance, session->ctx);
    }

    free(session->pending.surrounding_text);
    free(session->current.surrounding_text);
    free(session);
}

void typio_wl_session_reset(TypioWlSession *session) {
    if (!session) {
        return;
    }

    /* Reset pending state */
    free(session->pending.surrounding_text);
    session->pending.surrounding_text = NULL;
    session->pending.cursor = 0;
    session->pending.anchor = 0;
    session->pending.content_hint = 0;
    session->pending.content_purpose = 0;
    session->pending.text_change_cause = 0;
    session->pending.active = false;
}

void typio_wl_session_apply_pending(TypioWlSession *session) {
    if (!session) {
        return;
    }

    /* Apply surrounding text */
    free(session->current.surrounding_text);
    session->current.surrounding_text = session->pending.surrounding_text;
    session->current.cursor = session->pending.cursor;
    session->current.anchor = session->pending.anchor;
    session->pending.surrounding_text = NULL;

    /* Apply content type */
    session->current.content_hint = session->pending.content_hint;
    session->current.content_purpose = session->pending.content_purpose;

    /* Update context with surrounding text if available */
    if (session->current.surrounding_text && session->ctx) {
        typio_input_context_set_surrounding(session->ctx,
                                            session->current.surrounding_text,
                                            (int)session->current.cursor,
                                            (int)session->current.anchor);
    }

    /* Increment serial */
    session->serial++;
}

/* Commit helpers */
void typio_wl_commit_string(TypioWlFrontend *frontend, const char *text) {
    if (!frontend || !frontend->input_method || !text) {
        return;
    }
    zwp_input_method_v2_commit_string(frontend->input_method, text);
}

void typio_wl_set_preedit(TypioWlFrontend *frontend, const char *text,
                          int cursor_begin, int cursor_end) {
    if (!frontend || !frontend->input_method) {
        return;
    }
    zwp_input_method_v2_set_preedit_string(frontend->input_method,
                                           text ? text : "",
                                           cursor_begin, cursor_end);
}

void typio_wl_commit(TypioWlFrontend *frontend) {
    if (!frontend || !frontend->input_method || !frontend->session) {
        return;
    }
    zwp_input_method_v2_commit(frontend->input_method, frontend->session->serial);
}

/* Input method event handlers */
static void im_handle_activate(void *data, struct zwp_input_method_v2 *im) {
    TypioWlFrontend *frontend = data;
    (void)im;

    typio_wl_trace(frontend, "im", "event=activate");

    /* Create session if needed */
    if (!frontend->session) {
        frontend->session = typio_wl_session_create(frontend);
        if (!frontend->session) {
            typio_log(TYPIO_LOG_ERROR, "Failed to create session on activate");
            return;
        }
    }

    if (!typio_wl_lifecycle_should_defer_activate(session_is_focused(frontend))) {
        set_pending_reactivation(frontend, false);
        typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_ACTIVATING,
                                     "activate event");
    } else {
        set_pending_reactivation(frontend, true);
        typio_wl_trace(frontend,
                       "lifecycle",
                       "action=defer_reactivation phase=%s reason=%s",
                       typio_wl_lifecycle_phase_name(frontend->lifecycle_phase),
                       "activate while already focused");
    }

    /* Reset session state for new activation */
    typio_wl_session_reset(frontend->session);
    frontend->session->pending.active = true;
}

static void im_handle_deactivate(void *data, struct zwp_input_method_v2 *im) {
    TypioWlFrontend *frontend = data;
    (void)im;

    typio_wl_trace(frontend, "im", "event=deactivate");
    set_pending_reactivation(frontend, false);
    typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_DEACTIVATING, "deactivate event");
    typio_wl_lifecycle_hard_reset_keyboard(frontend, "deactivate event");

    if (frontend->session) {
        frontend->session->pending.active = false;
    }
}

static void im_handle_surrounding_text(void *data, struct zwp_input_method_v2 *im,
                                       const char *text, uint32_t cursor,
                                       uint32_t anchor) {
    TypioWlFrontend *frontend = data;
    (void)im;

    typio_wl_trace(frontend,
                   "im",
                   "event=surrounding_text cursor=%u anchor=%u has_text=%s",
                   cursor, anchor, text ? "yes" : "no");

    if (!frontend->session) {
        return;
    }

    free(frontend->session->pending.surrounding_text);
    frontend->session->pending.surrounding_text = text ? typio_strdup(text) : NULL;
    frontend->session->pending.cursor = cursor;
    frontend->session->pending.anchor = anchor;
}

static void im_handle_text_change_cause(void *data, struct zwp_input_method_v2 *im,
                                        uint32_t cause) {
    TypioWlFrontend *frontend = data;
    (void)im;

    typio_wl_trace(frontend, "im", "event=text_change_cause cause=%u", cause);

    if (frontend->session) {
        frontend->session->pending.text_change_cause = cause;
    }
}

static void im_handle_content_type(void *data, struct zwp_input_method_v2 *im,
                                   uint32_t hint, uint32_t purpose) {
    TypioWlFrontend *frontend = data;
    (void)im;

    typio_wl_trace(frontend,
                   "im",
                   "event=content_type hint=0x%x purpose=%u",
                   hint, purpose);

    if (frontend->session) {
        frontend->session->pending.content_hint = hint;
        frontend->session->pending.content_purpose = purpose;
    }
}

static void im_handle_done(void *data, struct zwp_input_method_v2 *im) {
    TypioWlFrontend *frontend = data;
    bool needs_reactivation;
    (void)im;

    if (!frontend->session) {
        return;
    }

    bool was_active = session_is_focused(frontend);
    bool now_active = frontend->session->pending.active;
    needs_reactivation = typio_wl_lifecycle_should_commit_reactivation(
        frontend->pending_reactivation, was_active, now_active);

    /* Apply pending state */
    typio_wl_session_apply_pending(frontend->session);

    /* Handle focus changes */
    if (now_active && !was_active) {
        TypioEngine *engine = active_engine(frontend);

        typio_log(TYPIO_LOG_INFO, "Input context focused");
        typio_input_context_focus_in(frontend->session->ctx);

        if (!engine) {
            typio_log(TYPIO_LOG_WARNING, "No active engine, skipping keyboard grab");
            typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_ACTIVE,
                                         "focus in without active engine");
            set_pending_reactivation(frontend, false);
            return;
        }

        if (!rebuild_keyboard_grab(frontend,
                                   "focus in before new grab",
                                   "Failed to create keyboard grab on activation")) {
            typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_INACTIVE,
                                         "focus in keyboard create failed");
            set_pending_reactivation(frontend, false);
            return;
        }

        typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_ACTIVE, "focus in complete");
        set_pending_reactivation(frontend, false);
    } else if (needs_reactivation) {
        TypioEngine *engine = active_engine(frontend);

        typio_wl_trace(frontend,
                       "lifecycle",
                       "action=commit_reactivation reason=done event");

        if (!engine) {
            typio_log(TYPIO_LOG_WARNING,
                      "No active engine, skipping keyboard reactivation");
            set_pending_reactivation(frontend, false);
            return;
        }

        if (!rebuild_keyboard_grab(frontend,
                                   "reactivation done",
                                   "Failed to recreate keyboard grab on reactivation")) {
            typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_INACTIVE,
                                         "reactivation keyboard create failed");
            set_pending_reactivation(frontend, false);
            return;
        }

        set_pending_reactivation(frontend, false);
    } else if (!now_active && was_active) {
        typio_log(TYPIO_LOG_INFO, "Input context unfocused");
        typio_input_context_focus_out(frontend->session->ctx);
        typio_input_context_reset(frontend->session->ctx);
        typio_wl_popup_hide(frontend);

        typio_wl_lifecycle_hard_reset_keyboard(frontend, "focus out");
        typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_INACTIVE, "focus out complete");
        set_pending_reactivation(frontend, false);
    }
}

static void im_handle_unavailable(void *data, struct zwp_input_method_v2 *im) {
    TypioWlFrontend *frontend = data;
    (void)im;

    typio_log(TYPIO_LOG_WARNING, "Input method unavailable - another IM may be active");

    /* Stop the frontend */
    frontend->running = false;
    snprintf(frontend->error_msg, sizeof(frontend->error_msg),
             "Input method unavailable - another input method may be active");
}

/* Typio callbacks */
static void on_commit_callback(TypioInputContext *ctx, const char *text,
                               void *user_data) {
    (void)ctx;
    TypioWlSession *session = user_data;

    if (!session || !text || !text[0]) {
        return;
    }

    typio_log(TYPIO_LOG_DEBUG, "Commit: %s", text);

    /* Clear preedit first */
    typio_wl_set_preedit(session->frontend, "", -1, -1);
    typio_wl_popup_hide(session->frontend);

    /* Commit the text */
    typio_wl_commit_string(session->frontend, text);

    /* Apply changes */
    typio_wl_commit(session->frontend);
}

static void on_preedit_callback(TypioInputContext *ctx,
                                const TypioPreedit *preedit, void *user_data) {
    (void)ctx;
    (void)preedit;
    (void)user_data;
    /* Preedit changes are rendered together with candidates in
     * on_candidate_callback.  Rendering here would consume a popup
     * buffer with stale candidate data before the candidate callback
     * fires, causing alternating popup/inline on every keystroke. */
}

static void on_candidate_callback(TypioInputContext *ctx,
                                  const TypioCandidateList *candidates,
                                  void *user_data) {
    (void)candidates;
    TypioWlSession *session = user_data;

    if (!session) {
        return;
    }

    update_wayland_text_ui(session, ctx);
}

static void update_wayland_text_ui(TypioWlSession *session, TypioInputContext *ctx) {
    const TypioPreedit *preedit;
    const TypioCandidateList *candidates;
    char *plain_text;
    int cursor_pos = -1;
    bool has_candidates;

    if (!session || !ctx) {
        return;
    }

    candidates = typio_input_context_get_candidates(ctx);
    has_candidates = candidates && candidates->count > 0;

    if (!has_candidates && !typio_wl_popup_is_available(session->frontend)) {
        update_inline_ui(session, ctx);
        return;
    }

    preedit = typio_input_context_get_preedit(ctx);
    plain_text = typio_wl_build_plain_preedit(preedit, &cursor_pos);
    if (!plain_text) {
        typio_wl_set_preedit(session->frontend, "", -1, -1);
    } else {
        typio_wl_set_preedit(session->frontend, plain_text, cursor_pos, cursor_pos);
        free(plain_text);
    }

    if (!typio_wl_popup_update(session->frontend, ctx)) {
        update_inline_ui(session, ctx);
        return;
    }

    typio_wl_commit(session->frontend);
}

static void update_inline_ui(TypioWlSession *session, TypioInputContext *ctx) {
    const TypioPreedit *preedit;
    const TypioCandidateList *candidates;
    char *inline_text;
    int cursor_pos = -1;

    if (!session || !ctx) {
        return;
    }

    preedit = typio_input_context_get_preedit(ctx);
    candidates = typio_input_context_get_candidates(ctx);
    inline_text = typio_wl_build_inline_preedit(preedit, candidates, &cursor_pos);
    if (!inline_text) {
        typio_wl_set_preedit(session->frontend, "", -1, -1);
        typio_wl_commit(session->frontend);
        return;
    }

    typio_log(TYPIO_LOG_DEBUG, "Inline UI: %s (cursor=%d)", inline_text, cursor_pos);
    typio_wl_set_preedit(session->frontend, inline_text, cursor_pos, cursor_pos);
    typio_wl_commit(session->frontend);
    free(inline_text);
}
