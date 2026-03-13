#include "typio/typio.h"
#include "typio_build_config.h"

static TypioResult basic_init(TypioEngine *engine, TypioInstance *instance) {
    (void)engine;
    (void)instance;
    return TYPIO_OK;
}

static void basic_destroy(TypioEngine *engine) {
    (void)engine;
}

static bool basic_has_blocking_modifiers(const TypioKeyEvent *event) {
    return event && (event->modifiers & (TYPIO_MOD_CTRL | TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) != 0;
}

static size_t basic_encode_utf8(uint32_t codepoint, char out[5]) {
    if (codepoint <= 0x7F) {
        out[0] = (char)codepoint;
        out[1] = '\0';
        return 1;
    }

    if (codepoint <= 0x7FF) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        out[2] = '\0';
        return 2;
    }

    if (codepoint <= 0xFFFF) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        out[3] = '\0';
        return 3;
    }

    if (codepoint <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        out[4] = '\0';
        return 4;
    }

    out[0] = '\0';
    return 0;
}

static TypioKeyProcessResult basic_process_key(TypioEngine *engine,
                                               TypioInputContext *ctx,
                                               const TypioKeyEvent *event) {
    (void)engine;

    if (!ctx || !event || event->type != TYPIO_EVENT_KEY_PRESS) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    if (typio_key_event_is_modifier_only(event) || basic_has_blocking_modifiers(event)) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    uint32_t codepoint = typio_key_event_get_unicode(event);
    if (codepoint < 0x20 || codepoint == 0x7F) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    char utf8[5];
    if (basic_encode_utf8(codepoint, utf8) == 0) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    typio_input_context_commit(ctx, utf8);
    return TYPIO_KEY_COMMITTED;
}

static const TypioEngineInfo basic_engine_info = {
    .name = "basic",
    .display_name = "Basic Keyboard",
    .description = "Built-in basic keyboard engine that commits printable text directly.",
    .version = TYPIO_VERSION,
    .author = "Typio",
    .icon = "typio-keyboard",
    .language = "und",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
    .capabilities = TYPIO_CAP_NONE,
    .api_version = TYPIO_API_VERSION,
};

static const TypioEngineOps basic_engine_ops = {
    .init = basic_init,
    .destroy = basic_destroy,
    .process_key = basic_process_key,
};

const TypioEngineInfo *typio_engine_get_info_basic(void) {
    return &basic_engine_info;
}

TypioEngine *typio_engine_create_basic(void) {
    return typio_engine_new(&basic_engine_info, &basic_engine_ops);
}
