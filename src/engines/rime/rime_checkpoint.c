/**
 * @file rime_checkpoint.c
 * @brief Session snapshot/restore so an in-flight composition survives a
 *        daemon restart (engine side of the Stage 3a checkpoint feature).
 *
 * The resumable state for a Rime composition is its raw input buffer (the
 * keystrokes typed so far, e.g. "nihao") plus the ascii_mode flag. The
 * blob format is engine-private:
 *
 *   byte 0     : ascii_mode, '0' or '1'
 *   bytes 1..N : raw input string (UTF-8, no trailing NUL)
 *
 * On restore the input is replayed through the current schema with
 * set_input, which regenerates the preedit and candidate list exactly as
 * if the user had retyped it.
 *
 * Both ops degrade safely on older librime: get_input/set_input are gated
 * by RIME_API_AVAILABLE (which checks the loaded library's data_size), so
 * a runtime without them simply persists/restores nothing rather than
 * calling past the end of the RimeApi struct.
 */

#include "rime_internal.h"

/* RIME_API_AVAILABLE expands to a pointer-difference compared against
 * (int)data_size, which trips -Wsign-conversion inside the system header.
 * Isolate the suppression to these one-line probes. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
static inline bool rime_has_get_input(RimeApi *api) {
    return RIME_API_AVAILABLE(api, get_input);
}
static inline bool rime_has_set_input(RimeApi *api) {
    return RIME_API_AVAILABLE(api, set_input);
}
#pragma GCC diagnostic pop

TypioResult typio_rime_snapshot_session(TypioEngine *engine,
                                        TypioInputContext *ctx,
                                        char **out_data,
                                        size_t *out_size) {
    TypioRimeState *state;
    TypioRimeSession *session;
    const char *input;
    size_t len;
    char *blob;
    Bool ascii;

    if (out_data)
        *out_data = nullptr;
    if (out_size)
        *out_size = 0;
    if (!engine || !ctx || !out_data || !out_size)
        return TYPIO_ERROR_INVALID_ARGUMENT;

    state = typio_engine_get_user_data(engine);
    if (!state || !state->api)
        return TYPIO_ERROR_NOT_INITIALIZED;

    /* Don't create a session just to snapshot — if none exists there is no
     * composition to persist. */
    session = typio_rime_get_session(engine, ctx, false);
    if (!session)
        return TYPIO_OK;

    if (!rime_has_get_input(state->api))
        return TYPIO_OK;

    input = state->api->get_input(session->session_id);
    if (!input || !input[0])
        return TYPIO_OK; /* nothing in flight */

    len = strlen(input);
    blob = malloc(1 + len);
    if (!blob)
        return TYPIO_ERROR_OUT_OF_MEMORY;

    ascii = state->api->get_option(session->session_id, "ascii_mode");
    blob[0] = ascii ? '1' : '0';
    memcpy(blob + 1, input, len);

    *out_data = blob;
    *out_size = 1 + len;
    return TYPIO_OK;
}

TypioResult typio_rime_restore_session(TypioEngine *engine,
                                       TypioInputContext *ctx,
                                       const char *data,
                                       size_t size) {
    TypioRimeState *state;
    TypioRimeSession *session;
    bool ascii;
    size_t input_len;
    char *input;
    Bool ok;

    if (!engine || !ctx || !data || size < 1)
        return TYPIO_ERROR_INVALID_ARGUMENT;

    state = typio_engine_get_user_data(engine);
    if (!state || !state->api)
        return TYPIO_ERROR_NOT_INITIALIZED;

    if (!rime_has_set_input(state->api))
        return TYPIO_ERROR;

    session = typio_rime_get_session(engine, ctx, true);
    if (!session)
        return TYPIO_ERROR;

    ascii = (data[0] == '1');
    state->api->set_option(session->session_id, "ascii_mode", ascii ? True : False);

    /* The blob's input is not NUL-terminated; copy into a terminated buffer
     * for the C string API. */
    input_len = size - 1;
    input = malloc(input_len + 1);
    if (!input)
        return TYPIO_ERROR_OUT_OF_MEMORY;
    memcpy(input, data + 1, input_len);
    input[input_len] = '\0';

    ok = state->api->set_input(session->session_id, input);
    free(input);
    if (!ok)
        return TYPIO_ERROR;

    /* Push the regenerated preedit/candidates to the context and refresh
     * the mode indicator so the resumed composition is visible immediately. */
    typio_rime_sync_context(session, ctx);
    typio_rime_notify_mode(engine, session, ascii);
    return TYPIO_OK;
}
