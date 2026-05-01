#include "typio/typio.h"
#include "typio_build_config.h"
#include "compose.h"

#include <stdlib.h>
#include <string.h>

/* Engine-specific per-instance data */
typedef struct {
    BasicCompose *compose;
    bool compose_enabled;
} BasicEngineData;

static BasicEngineData *basic_get_data(TypioEngine *engine) {
    return (BasicEngineData *)typio_engine_get_user_data(engine);
}

static TypioResult basic_init([[maybe_unused]] TypioEngine *engine,
                               TypioInstance *instance) {
    BasicEngineData *data = calloc(1, sizeof(BasicEngineData));
    if (!data) {
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    data->compose = basic_compose_new();
    if (!data->compose) {
        free(data);
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    data->compose_enabled = false;
    if (instance) {
        TypioConfig *config = typio_instance_get_config(instance);
        if (config) {
            data->compose_enabled = typio_config_get_bool(config,
                                                           "engines.basic.compose",
                                                           false);
        }
    }

    typio_engine_set_user_data(engine, data);
    return TYPIO_OK;
}

static void basic_destroy(TypioEngine *engine) {
    BasicEngineData *data = basic_get_data(engine);
    if (data) {
        if (data->compose) {
            basic_compose_free(data->compose);
        }
        free(data);
        typio_engine_set_user_data(engine, NULL);
    }
}

static void basic_focus_in([[maybe_unused]] TypioEngine *engine,
                            [[maybe_unused]] TypioInputContext *ctx) {
    /* Basic engine has no per-context UI state to restore. */
}

static void basic_reset(TypioEngine *engine, TypioInputContext *ctx) {
    BasicEngineData *data = basic_get_data(engine);
    if (!data || !data->compose) {
        return;
    }
    if (basic_compose_is_active(data->compose) && ctx) {
        typio_input_context_clear_preedit(ctx);
    }
    basic_compose_reset(data->compose);
}

static void basic_focus_out(TypioEngine *engine, TypioInputContext *ctx) {
    /* Cancel any pending composition on focus loss to avoid stale preedit. */
    basic_reset(engine, ctx);
}

static TypioResult basic_reload_config([[maybe_unused]] TypioEngine *engine) {
    /* Basic engine has no runtime-reloadable configuration. */
    return TYPIO_OK;
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

static void basic_commit_codepoint(TypioInputContext *ctx, uint32_t codepoint) {
    char utf8[5];
    if (basic_encode_utf8(codepoint, utf8) > 0) {
        typio_input_context_commit(ctx, utf8);
    }
}

static void basic_update_preedit(TypioInputContext *ctx, const BasicCompose *compose) {
    const char *text = basic_compose_get_preedit(compose);
    if (text) {
        TypioPreeditSegment segment = {
            .text = text,
            .format = TYPIO_PREEDIT_UNDERLINE,
        };
        TypioPreedit preedit = {
            .segments = &segment,
            .segment_count = 1,
            .cursor_pos = -1,
        };
        typio_input_context_set_preedit(ctx, &preedit);
    } else {
        typio_input_context_clear_preedit(ctx);
    }
}

static TypioKeyProcessResult basic_process_key(TypioEngine *engine,
                                                TypioInputContext *ctx,
                                                const TypioKeyEvent *event) {
    if (!ctx || !event || event->type != TYPIO_EVENT_KEY_PRESS) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    if (typio_key_event_is_modifier_only(event) || basic_has_blocking_modifiers(event)) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    BasicEngineData *data = basic_get_data(engine);

    /* Escape cancels an active composition. */
    if (data && data->compose && basic_compose_is_active(data->compose)
        && typio_key_event_is_escape(event)) {
        basic_compose_cancel(data->compose);
        typio_input_context_clear_preedit(ctx);
        return TYPIO_KEY_HANDLED;
    }

    uint32_t codepoint = typio_key_event_get_unicode(event);

    /* Non-printable keys: if composing, cancel first then let the key pass through. */
    if (codepoint < 0x20 || codepoint == 0x7F) {
        if (data && data->compose && basic_compose_is_active(data->compose)) {
            uint32_t cp = basic_compose_cancel(data->compose);
            typio_input_context_clear_preedit(ctx);
            if (cp) {
                basic_commit_codepoint(ctx, cp);
                return TYPIO_KEY_COMMITTED;
            }
            return TYPIO_KEY_HANDLED;
        }
        return TYPIO_KEY_NOT_HANDLED;
    }

    /* Fast path: compose disabled or unavailable. */
    if (!data || !data->compose_enabled || !data->compose) {
        char utf8[5];
        if (basic_encode_utf8(codepoint, utf8) == 0) {
            return TYPIO_KEY_NOT_HANDLED;
        }
        typio_input_context_commit(ctx, utf8);
        return TYPIO_KEY_COMMITTED;
    }

    uint32_t out_cps[4];
    size_t out_count = 0;
    BasicComposeResult result = basic_compose_process_key(data->compose, codepoint,
                                                           out_cps, &out_count);

    switch (result) {
    case BASIC_COMPOSE_NONE:
        /* Not a compose sequence: commit the key directly. */
        basic_commit_codepoint(ctx, codepoint);
        return TYPIO_KEY_COMMITTED;

    case BASIC_COMPOSE_CONSUME:
        /* First key of a compose sequence consumed. */
        basic_update_preedit(ctx, data->compose);
        return TYPIO_KEY_COMPOSING;

    case BASIC_COMPOSE_COMMIT: {
        /* Complete compose sequence. */
        typio_input_context_clear_preedit(ctx);
        for (size_t i = 0; i < out_count; i++) {
            basic_commit_codepoint(ctx, out_cps[i]);
        }
        return TYPIO_KEY_COMMITTED;
    }

    case BASIC_COMPOSE_CANCEL: {
        /* No matching rule: flush the buffered first key, then commit current key. */
        typio_input_context_clear_preedit(ctx);
        for (size_t i = 0; i < out_count; i++) {
            basic_commit_codepoint(ctx, out_cps[i]);
        }
        basic_commit_codepoint(ctx, codepoint);
        return TYPIO_KEY_COMMITTED;
    }
    }

    return TYPIO_KEY_NOT_HANDLED;
}

static const TypioEngineInfo basic_engine_info = {
    .name = "basic",
    .display_name = "Basic",
    .description = "Built-in basic keyboard engine that commits printable text directly.",
    .version = TYPIO_VERSION,
    .author = "Typio",
    .icon = "typio-keyboard",
    .language = "und",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
    .capabilities = TYPIO_CAP_NONE,
    .api_version = TYPIO_API_VERSION,
    .struct_size = TYPIO_ENGINE_INFO_SIZE,
};

static const TypioEngineBaseOps basic_base_ops = {
    .init = basic_init,
    .destroy = basic_destroy,
    .focus_in = basic_focus_in,
    .focus_out = basic_focus_out,
    .reset = basic_reset,
    .reload_config = basic_reload_config,
};

static const TypioKeyboardEngineOps basic_keyboard_ops = {
    .process_key = basic_process_key,
};

const TypioEngineInfo *typio_engine_get_info_basic(void) {
    return &basic_engine_info;
}

TypioEngine *typio_engine_create_basic(void) {
    return typio_engine_new(&basic_engine_info, &basic_base_ops,
                            &basic_keyboard_ops, nullptr);
}
