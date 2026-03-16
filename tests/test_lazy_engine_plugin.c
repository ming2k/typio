#include "typio/engine.h"

static TypioResult lazy_plugin_init([[maybe_unused]] TypioEngine *engine,
                                    [[maybe_unused]] TypioInstance *instance) {
    return TYPIO_OK;
}

static void lazy_plugin_destroy([[maybe_unused]] TypioEngine *engine) {
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

static const TypioEngineOps lazy_plugin_ops = {
    .init = lazy_plugin_init,
    .destroy = lazy_plugin_destroy,
    .process_key = lazy_plugin_process_key,
};

static TypioEngine *lazy_plugin_create(void) {
    return typio_engine_new(&lazy_plugin_info, &lazy_plugin_ops);
}

TYPIO_ENGINE_DEFINE(lazy_plugin_info, lazy_plugin_create)
