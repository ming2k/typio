/**
 * @file wl_input_method.c
 * @brief zwp_input_method_v2 event handlers
 */

#include "wl_frontend_internal.h"
#include "identity.h"
#include "monotonic_time.h"
#include "preedit_format.h"
#include "text_ui_state.h"
#include "wl_trace.h"
#include "typio/typio.h"
#include "utils/log.h"
#include "utils/string.h"

#include <stdlib.h>
#include <string.h>

#define TYPIO_WL_UI_SLOW_UPDATE_MS 8

/* Forward declarations for callbacks */
static void on_commit_callback(TypioInputContext *ctx, const char *text,
                               void *user_data);
static void on_preedit_callback(TypioInputContext *ctx,
                                const TypioPreedit *preedit, void *user_data);
static void on_candidate_callback(TypioInputContext *ctx,
                                  const TypioCandidateList *candidates,
                                  void *user_data);
static void update_wayland_text_ui(TypioWlSession *session, TypioInputContext *ctx);

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

static bool frontend_has_non_routable_grab(TypioWlFrontend *frontend,
                                           bool now_active) {
    if (!frontend || !frontend->keyboard) {
        return false;
    }

    return !now_active || !session_is_focused(frontend) ||
           frontend->lifecycle_phase != TYPIO_WL_PHASE_ACTIVE;
}

static TypioEngine *active_engine(TypioWlFrontend *frontend) {
    TypioEngineManager *manager;

    if (!frontend || !frontend->instance) {
        return nullptr;
    }

    manager = typio_instance_get_engine_manager(frontend->instance);
    return manager ? typio_engine_manager_get_active(manager) : nullptr;
}

static void trace_session_state(TypioWlFrontend *frontend, const char *event) {
    bool focused;
    bool pending_active;
    uint32_t serial;

    if (!frontend) {
        return;
    }

    focused = session_is_focused(frontend);
    pending_active = frontend->session ? frontend->session->pending.active : false;
    serial = frontend->im_serial;

    typio_wl_trace(frontend,
                   "im_state",
                   "event=%s phase=%s focused=%s pending_active=%s pending_reactivation=%s session=%s keyboard=%s serial=%u",
                   event ? event : "unknown",
                   typio_wl_lifecycle_phase_name(frontend->lifecycle_phase),
                   focused ? "yes" : "no",
                   pending_active ? "yes" : "no",
                   frontend->pending_reactivation ? "yes" : "no",
                   frontend->session ? "yes" : "no",
                   frontend->keyboard ? "yes" : "no",
                   serial);
}

static bool rebuild_keyboard_grab(TypioWlFrontend *frontend,
                                  const char *reset_reason,
                                  const char *failure_reason) {
    if (!frontend) {
        return false;
    }

    bool had_keyboard = frontend->keyboard != NULL;
    typio_wl_trace(frontend,
                   "keyboard_grab",
                   "action=rebuild begin reason=%s phase=%s focused=%s existing_keyboard=%s",
                   reset_reason ? reset_reason : "keyboard rebuild",
                   typio_wl_lifecycle_phase_name(frontend->lifecycle_phase),
                   session_is_focused(frontend) ? "yes" : "no",
                   had_keyboard ? "yes" : "no");
    typio_wl_lifecycle_hard_reset_keyboard(frontend,
                                           reset_reason ? reset_reason : "keyboard rebuild");
    frontend->keyboard = typio_wl_keyboard_create(frontend);
    if (!frontend->keyboard) {
        typio_log(TYPIO_LOG_ERROR,
                  "%s", failure_reason ? failure_reason : "Failed to rebuild keyboard grab");
        typio_wl_trace_level(TYPIO_LOG_ERROR,
                             frontend,
                             "keyboard_grab",
                             "action=rebuild result=failed reason=%s phase=%s focused=%s",
                             failure_reason ? failure_reason : "Failed to rebuild keyboard grab",
                             typio_wl_lifecycle_phase_name(frontend->lifecycle_phase),
                             session_is_focused(frontend) ? "yes" : "no");
        return false;
    }

    typio_wl_vk_expect_keymap(frontend, "keyboard grab rebuilt");

    /* Only suppress stale keys when rebuilding an existing grab.
     * When no previous grab existed (fresh activation after auto-focus),
     * the re-sent key is the first time the IME sees it — not stale. */
    if (!had_keyboard) {
        frontend->keyboard->suppress_stale_keys = false;
    }

    typio_wl_trace(frontend,
                   "keyboard_grab",
                   "action=rebuild result=ok created_at_epoch=%" PRIu64 " suppress_stale_keys=%s",
                   frontend->keyboard->created_at_epoch,
                   frontend->keyboard->suppress_stale_keys ? "yes" : "no");
    return true;
}

/* Session management */
TypioWlSession *typio_wl_session_create(TypioWlFrontend *frontend) {
    TypioWlSession *session = calloc(1, sizeof(TypioWlSession));
    if (!session) {
        return nullptr;
    }

    session->frontend = frontend;

    /* Create input context */
    session->ctx = typio_instance_create_context(frontend->instance);
    if (!session->ctx) {
        typio_log(TYPIO_LOG_ERROR, "Failed to create input context");
        free(session);
        return nullptr;
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

    free(session->last_preedit_text);
    free(session->pending.surrounding_text);
    free(session->current.surrounding_text);
    free(session);
}

void typio_wl_session_reset(TypioWlSession *session) {
    if (!session) {
        return;
    }

    /* Reset preedit change tracking and cancel any deferred popup work from
     * the previous activation so stale candidates cannot be redrawn later. */
    typio_wl_text_ui_reset_tracking(session->frontend ? &session->frontend->popup_update_pending
                                                      : nullptr,
                                    &session->last_preedit_text,
                                    &session->last_preedit_cursor);

    /* Reset pending state */
    free(session->pending.surrounding_text);
    session->pending.surrounding_text = nullptr;
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
    session->pending.surrounding_text = nullptr;

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
    zwp_input_method_v2_commit(frontend->input_method, frontend->im_serial);
}

/* Input method event handlers */
static void im_handle_activate(void *data, [[maybe_unused]] struct zwp_input_method_v2 *im) {
    TypioWlFrontend *frontend = data;

    typio_wl_trace(frontend, "im", "event=activate");
    trace_session_state(frontend, "activate_begin");

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
    trace_session_state(frontend, "activate_end");
}

static void im_handle_deactivate(void *data, [[maybe_unused]] struct zwp_input_method_v2 *im) {
    TypioWlFrontend *frontend = data;

    typio_wl_trace(frontend, "im", "event=deactivate");
    trace_session_state(frontend, "deactivate_begin");
    set_pending_reactivation(frontend, false);
    typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_DEACTIVATING, "deactivate event");
    typio_wl_trace(frontend,
                   "lifecycle",
                   "action=defer_hard_reset reason=deactivate event phase=%s",
                   typio_wl_lifecycle_phase_name(frontend->lifecycle_phase));

    if (frontend->session) {
        frontend->session->pending.active = false;
    }
    trace_session_state(frontend, "deactivate_end");
}

static void im_handle_surrounding_text(void *data, [[maybe_unused]] struct zwp_input_method_v2 *im,
                                       const char *text, uint32_t cursor,
                                       uint32_t anchor) {
    TypioWlFrontend *frontend = data;

    typio_wl_trace(frontend,
                   "im",
                   "event=surrounding_text cursor=%u anchor=%u has_text=%s",
                   cursor, anchor, text ? "yes" : "no");

    if (!frontend->session) {
        return;
    }

    free(frontend->session->pending.surrounding_text);
    frontend->session->pending.surrounding_text = text ? typio_strdup(text) : nullptr;
    frontend->session->pending.cursor = cursor;
    frontend->session->pending.anchor = anchor;
}

static void im_handle_text_change_cause(void *data, [[maybe_unused]] struct zwp_input_method_v2 *im,
                                        uint32_t cause) {
    TypioWlFrontend *frontend = data;

    typio_wl_trace(frontend, "im", "event=text_change_cause cause=%u", cause);

    if (frontend->session) {
        frontend->session->pending.text_change_cause = cause;
    }
}

static void im_handle_content_type(void *data, [[maybe_unused]] struct zwp_input_method_v2 *im,
                                   uint32_t hint, uint32_t purpose) {
    TypioWlFrontend *frontend = data;

    typio_wl_trace(frontend,
                   "im",
                   "event=content_type hint=0x%x purpose=%u",
                   hint, purpose);

    if (frontend->session) {
        frontend->session->pending.content_hint = hint;
        frontend->session->pending.content_purpose = purpose;
    }
}

static void im_handle_done(void *data, [[maybe_unused]] struct zwp_input_method_v2 *im) {
    TypioWlFrontend *frontend = data;
    bool needs_reactivation;

    frontend->im_serial++;

    if (!frontend->session) {
        typio_log(TYPIO_LOG_WARNING,
                  "Received done event without session (serial=%u)",
                  frontend->im_serial);
        return;
    }

    trace_session_state(frontend, "done_begin");
    bool was_active = session_is_focused(frontend);
    bool now_active = frontend->session->pending.active;
    bool needs_cleanup = typio_wl_lifecycle_should_cleanup_on_done(was_active,
                                                                   now_active);
    needs_reactivation = typio_wl_lifecycle_should_commit_reactivation(
        frontend->pending_reactivation, was_active, now_active);

    typio_wl_trace(frontend,
                   "im_done",
                   "was_active=%s now_active=%s needs_reactivation=%s phase=%s",
                   was_active ? "yes" : "no",
                   now_active ? "yes" : "no",
                   needs_reactivation ? "yes" : "no",
                   typio_wl_lifecycle_phase_name(frontend->lifecycle_phase));

    /* Apply pending state */
    typio_wl_session_apply_pending(frontend->session);

    /* Handle focus changes */
    if (now_active && !was_active) {
        TypioEngine *engine = active_engine(frontend);

        typio_log(TYPIO_LOG_INFO, "Input context focused");
        typio_input_context_focus_in(frontend->session->ctx);
        typio_wl_frontend_refresh_identity(frontend);
        typio_wl_frontend_restore_identity_engine(frontend);
        engine = active_engine(frontend);

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
        trace_session_state(frontend, "done_focus_in_complete");
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

        typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_ACTIVE,
                                     "reactivation complete");
        set_pending_reactivation(frontend, false);
        trace_session_state(frontend, "done_reactivation_complete");
    } else if (needs_cleanup) {
        typio_log(TYPIO_LOG_INFO, "Input context unfocused");
        typio_wl_text_ui_reset_tracking(&frontend->popup_update_pending,
                                        &frontend->session->last_preedit_text,
                                        &frontend->session->last_preedit_cursor);
        typio_input_context_focus_out(frontend->session->ctx);
        typio_input_context_reset(frontend->session->ctx);
        typio_wl_text_ui_backend_hide(frontend->text_ui_backend);

        typio_wl_lifecycle_hard_reset_keyboard(frontend, "focus out");
        typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_INACTIVE, "focus out complete");
        typio_wl_frontend_clear_identity(frontend);
        set_pending_reactivation(frontend, false);
        trace_session_state(frontend, "done_focus_out_complete");
    } else {
        if (frontend_has_non_routable_grab(frontend, now_active)) {
            typio_log(TYPIO_LOG_WARNING,
                      "Done completed without focus transition, but keyboard grab is still active in a non-routable state: phase=%s was_active=%s now_active=%s focused=%s pending_reactivation=%s",
                      typio_wl_lifecycle_phase_name(frontend->lifecycle_phase),
                      was_active ? "yes" : "no",
                      now_active ? "yes" : "no",
                      session_is_focused(frontend) ? "yes" : "no",
                      frontend->pending_reactivation ? "yes" : "no");
            if (!now_active || !session_is_focused(frontend)) {
                typio_log(TYPIO_LOG_WARNING,
                          "Recovering from stale non-routable keyboard grab after done without transition");
                typio_wl_lifecycle_hard_reset_keyboard(
                    frontend, "done no transition stale grab");
                typio_wl_lifecycle_set_phase(frontend, TYPIO_WL_PHASE_INACTIVE,
                                             "done no transition stale grab");
            }
        }
        trace_session_state(frontend, "done_no_transition");
    }
}

static void im_handle_unavailable(void *data, [[maybe_unused]] struct zwp_input_method_v2 *im) {
    TypioWlFrontend *frontend = data;

    typio_log(TYPIO_LOG_WARNING, "Input method unavailable - another IM may be active");

    /* Stop the frontend */
    frontend->running = false;
    snprintf(frontend->error_msg, sizeof(frontend->error_msg),
             "Input method unavailable - another input method may be active");
}

/* Typio callbacks */
static void on_commit_callback([[maybe_unused]] TypioInputContext *ctx, const char *text,
                               void *user_data) {
    TypioWlSession *session = user_data;

    if (!session || !text || !text[0]) {
        return;
    }

    typio_log(TYPIO_LOG_DEBUG, "Commit: %s", text);

    /* Clear preedit first */
    typio_wl_set_preedit(session->frontend, "", -1, -1);
    typio_wl_text_ui_backend_hide(session->frontend->text_ui_backend);

    /* Commit the text */
    typio_wl_commit_string(session->frontend, text);

    /* Apply changes */
    typio_wl_commit(session->frontend);

    typio_wl_text_ui_reset_tracking(&session->frontend->popup_update_pending,
                                    &session->last_preedit_text,
                                    &session->last_preedit_cursor);

    /* Notify the engine manager that the active engine committed text,
     * so the recent-engine pair used for slow-switch toggling stays current. */
    typio_engine_manager_notify_commit(
        typio_instance_get_engine_manager(session->frontend->instance));
}

static void on_preedit_callback([[maybe_unused]] TypioInputContext *ctx,
                                [[maybe_unused]] const TypioPreedit *preedit,
                                [[maybe_unused]] void *user_data) {
    /* Preedit changes are rendered together with candidates in
     * on_candidate_callback.  Rendering here would consume a popup
     * buffer with stale candidate data before the candidate callback
     * fires, causing alternating popup rendering on every keystroke. */
}

static void on_candidate_callback(TypioInputContext *ctx,
                                  [[maybe_unused]] const TypioCandidateList *candidates,
                                  void *user_data) {
    TypioWlSession *session = user_data;

    if (!session) {
        return;
    }

    update_wayland_text_ui(session, ctx);
}

static void update_wayland_text_ui(TypioWlSession *session, TypioInputContext *ctx) {
    const TypioPreedit *preedit;
    const TypioCandidateList *candidate_list;
    char *plain_text;
    int cursor_pos = -1;
    TypioWlTextUiPlan update_plan;
    uint64_t start_ms;
    uint64_t popup_done_ms;
    uint64_t end_ms;
    uint64_t popup_ms;
    uint64_t total_ms;

    if (!session || !ctx) {
        return;
    }

    start_ms = typio_wl_monotonic_ms();
    preedit = typio_input_context_get_preedit(ctx);
    candidate_list = typio_input_context_get_candidates(ctx);
    plain_text = typio_wl_build_plain_preedit(preedit, &cursor_pos);

    /* Detect whether the preedit actually changed compared to what we
     * last sent to the application.  When only the candidate highlight
     * moved (e.g. Up/Down navigation) the preedit stays identical and
     * we can skip the protocol commit, avoiding an expensive
     * composition-update round-trip in heavyweight clients like Chrome. */
    const char *new_text = plain_text ? plain_text : "";
    update_plan = typio_wl_text_ui_plan_update(session->last_preedit_text,
                                               session->last_preedit_cursor,
                                               new_text,
                                               cursor_pos);

    /* Keep the popup synchronous so candidate navigation updates the visible
     * highlight immediately. When the preedit is unchanged, skip the protocol
     * round-trip to the focused application and only refresh the popup. */
    typio_wl_text_ui_backend_update(session->frontend->text_ui_backend, ctx);
    popup_done_ms = typio_wl_monotonic_ms();

    session->frontend->popup_update_pending = false;
    if (update_plan == TYPIO_WL_TEXT_UI_SYNC_PREEDIT_AND_POPUP) {
        if (!plain_text) {
            typio_wl_set_preedit(session->frontend, "", -1, -1);
        } else {
            typio_wl_set_preedit(session->frontend, plain_text, cursor_pos, cursor_pos);
        }
        typio_wl_commit(session->frontend);
    }

    free(session->last_preedit_text);
    session->last_preedit_text = plain_text ? typio_strdup(new_text) : nullptr;
    session->last_preedit_cursor = cursor_pos;

    end_ms = typio_wl_monotonic_ms();
    popup_ms = (popup_done_ms >= start_ms) ? (popup_done_ms - start_ms) : 0;
    total_ms = (end_ms >= start_ms) ? (end_ms - start_ms) : 0;
    if (total_ms >= TYPIO_WL_UI_SLOW_UPDATE_MS) {
        typio_log_debug(
            "Wayland text UI slow: total=%" PRIu64 "ms popup=%" PRIu64 "ms preedit_changed=%s candidates=%zu selected=%d signature=%" PRIu64,
            total_ms,
            popup_ms,
            update_plan == TYPIO_WL_TEXT_UI_SYNC_PREEDIT_AND_POPUP ? "yes" : "no",
            candidate_list ? candidate_list->count : 0,
            candidate_list ? candidate_list->selected : -1,
            candidate_list ? candidate_list->content_signature : 0);
    }

    free(plain_text);
}
