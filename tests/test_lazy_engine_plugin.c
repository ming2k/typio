#include "typio/engine.h"

static TypioResult lazy_plugin_init([[maybe_unused]] TypioEngine *engine,
                                    [[maybe_unused]] TypioInstance *instance) {
    return TYPIO_OK;
}

static void lazy_plugin_destroy([[maybe_unused]] TypioEngine *engine) {
}

static void lazy_plugin_focus_in([[maybe_unused]] TypioEngine *engine,
                                  [[maybe_unused]] TypioInputContext *ctx) {
}

static void lazy_plugin_focus_out([[maybe_unused]] TypioEngine *engine,
                                   [[maybe_unused]] TypioInputContext *ctx) {
}

static void lazy_plugin_reset([[maybe_unused]] TypioEngine *engine,
                               [[maybe_unused]] TypioInputContext *ctx) {
}

static TypioResult lazy_plugin_reload_config([[maybe_unused]] TypioEngine *engine) {
    return TYPIO_OK;
}

static TypioKeyProcessResult lazy_plugin_process_key(
    [[maybe_unused]] TypioEngine *engine,
    [[maybe_unused]] TypioInputContext *ctx,
    [[maybe_unused]] const TypioKeyEvent *event) {
    return TYPIO_KEY_NOT_HANDLED;
}

static const TypioEngineInfo lazy_plugin_info = {
    .name = "lazy-plugin",
    .display_name = "Lazy Plugin",
    .description = "External plugin used to verify true lazy load/unload",
    .version = "1.0.0",
    .author = "Tests",
    .icon = "lazy-plugin",
    .language = "en",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
    .capabilities = TYPIO_CAP_PREEDIT,
    .api_version = TYPIO_API_VERSION,
};

static const TypioEngineBaseOps lazy_plugin_base_ops = {
    .init = lazy_plugin_init,
    .destroy = lazy_plugin_destroy,
    .focus_in = lazy_plugin_focus_in,
    .focus_out = lazy_plugin_focus_out,
    .reset = lazy_plugin_reset,
    .reload_config = lazy_plugin_reload_config,
};

static const TypioKeyboardEngineOps lazy_plugin_keyboard_ops = {
    .process_key = lazy_plugin_process_key,
};

static TypioEngine *lazy_plugin_create(void) {
    return typio_engine_new(&lazy_plugin_info, &lazy_plugin_base_ops,
                            &lazy_plugin_keyboard_ops, nullptr);
}

TYPIO_ENGINE_DEFINE(lazy_plugin_info, lazy_plugin_create)
