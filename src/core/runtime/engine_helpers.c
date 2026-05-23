#include "typio/engine.h"
#include "typio/input_context.h"

void _typio_engine_base_focus_in(TypioEngine *engine, TypioInputContext *ctx) {
    if (engine && engine->base_ops && engine->base_ops->focus_in) {
        engine->base_ops->focus_in(engine, ctx);
    }
}

void _typio_engine_base_focus_out(TypioEngine *engine, TypioInputContext *ctx) {
    if (engine && engine->base_ops && engine->base_ops->focus_out) {
        engine->base_ops->focus_out(engine, ctx);
    }
}

void _typio_engine_base_reset(TypioEngine *engine, TypioInputContext *ctx) {
    if (engine && engine->base_ops && engine->base_ops->reset) {
        engine->base_ops->reset(engine, ctx);
    }
}

TypioKeyProcessResult _typio_engine_keyboard_process_key(TypioEngine *engine, TypioInputContext *ctx, const TypioKeyEvent *event) {
    if (engine && engine->keyboard && engine->keyboard->process_key) {
        return engine->keyboard->process_key(engine, ctx, event);
    }
    return TYPIO_KEY_NOT_HANDLED;
}
