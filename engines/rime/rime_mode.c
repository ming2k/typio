/**
 * @file rime_mode.c
 * @brief Rime engine mode management (Chinese / ASCII)
 */

#include "rime_internal.h"

const TypioEngineMode typio_rime_mode_chinese = {
    .mode_class = TYPIO_MODE_CLASS_NATIVE,
    .mode_id = "chinese",
    .display_label = "中",
    .icon_name = "typio-rime",
};

const TypioEngineMode typio_rime_mode_latin = {
    .mode_class = TYPIO_MODE_CLASS_LATIN,
    .mode_id = "ascii",
    .display_label = "A",
    .icon_name = "typio-rime-latin",
};

const TypioEngineMode *typio_rime_mode_for_ascii(bool ascii_mode) {
    return ascii_mode ? &typio_rime_mode_latin : &typio_rime_mode_chinese;
}

void typio_rime_notify_mode(TypioEngine *engine,
                             TypioRimeSession *session,
                             bool ascii_mode) {
    if (!engine || !engine->instance || !session) {
        return;
    }

    session->ascii_mode_known = true;
    session->ascii_mode = ascii_mode;
    typio_instance_notify_mode(engine->instance, typio_rime_mode_for_ascii(ascii_mode));
}

void typio_rime_refresh_mode(TypioEngine *engine,
                              TypioRimeSession *session) {
    Bool ascii;

    if (!engine || !session || !session->state) {
        return;
    }

    ascii = session->state->api->get_option(session->session_id, "ascii_mode");
    typio_rime_notify_mode(engine, session, ascii ? true : false);
}

const TypioEngineMode *typio_rime_get_mode(TypioEngine *engine,
                                            TypioInputContext *ctx) {
    TypioRimeSession *session = typio_rime_get_session(engine, ctx, false);

    if (!session || !session->ascii_mode_known) {
        return &typio_rime_mode_chinese;
    }

    return typio_rime_mode_for_ascii(session->ascii_mode);
}

TypioResult typio_rime_set_mode(TypioEngine *engine,
                                 TypioInputContext *ctx,
                                 const char *mode_id) {
    TypioRimeSession *session;
    Bool ascii_mode;

    if (!engine || !ctx || !mode_id || !*mode_id) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    session = typio_rime_get_session(engine, ctx, true);
    if (!session || !session->state) {
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
