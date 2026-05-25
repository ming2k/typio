/**
 * @file status_service.c
 * @brief Transport-agnostic status & control service (JSON output).
 */

#include "state/service.h"
#include "ipc/tip_json.h"
#include "ipc/tip_protocol.h"

#include "typio/config.h"
#include "typio/engine_manager.h"
#include "typio/instance.h"
#include "typio/typio.h"
#include "typio_build_config.h"
#include "typio/log.h"
#include "typio/string.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TypioStatusService {
    TypioInstance *instance;
    TypioStatusServiceRuntimeStateCallback runtime_state_callback;
    void *runtime_state_user_data;
    TypioStatusServiceStopCallback stop_callback;
    void *stop_user_data;
    struct TypioStateController *state_controller;
};

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

static const char *engine_type_name(TypioEngineType type)
{
    return type == TYPIO_ENGINE_TYPE_VOICE ? "voice" : "keyboard";
}

static const char *mode_class_name(TypioModeClass cls)
{
    return cls == TYPIO_MODE_CLASS_LATIN ? "latin" : "native";
}

static TypioEngine *svc_active_keyboard_engine(TypioStatusService *svc)
{
    TypioEngineManager *manager;
    if (!svc || !svc->instance)
        return nullptr;
    manager = typio_instance_get_engine_manager(svc->instance);
    return manager ? typio_engine_manager_get_active(manager) : nullptr;
}

static TypioEngine *svc_active_voice_engine(TypioStatusService *svc)
{
    TypioEngineManager *manager;
    if (!svc || !svc->instance)
        return nullptr;
    manager = typio_instance_get_engine_manager(svc->instance);
    return manager ? typio_engine_manager_get_active_voice(manager) : nullptr;
}

/* ------------------------------------------------------------------ */
/*  JSON builders for individual properties                           */
/* ------------------------------------------------------------------ */

static char *build_version(TypioStatusService *svc)
{
    (void)svc;
    TipJsonBuilder *b = tip_json_builder_new();
    tip_json_builder_append_string(b, TYPIO_VERSION);
    return tip_json_builder_steal(b);
}

static char *build_active_engine(TypioStatusService *svc)
{
    TypioEngine *engine = svc_active_keyboard_engine(svc);
    const char *name = engine ? typio_engine_get_name(engine) : "";
    TipJsonBuilder *b = tip_json_builder_new();
    tip_json_builder_append_string(b, name);
    return tip_json_builder_steal(b);
}

static char *build_active_voice_engine(TypioStatusService *svc)
{
    TypioEngine *engine = svc_active_voice_engine(svc);
    const char *name = engine ? typio_engine_get_name(engine) : "";
    TipJsonBuilder *b = tip_json_builder_new();
    tip_json_builder_append_string(b, name);
    return tip_json_builder_steal(b);
}

static char *build_string_array(const char **items, size_t count)
{
    TipJsonBuilder *b = tip_json_builder_new();
    TIP_JSON_ARR_START(b);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) TIP_JSON_COMMA(b);
        tip_json_builder_append_string(b, items[i]);
    }
    TIP_JSON_ARR_END(b);
    return tip_json_builder_steal(b);
}

static char *build_available_keyboard_engines(TypioStatusService *svc)
{
    TypioEngineManager *manager = svc ? typio_instance_get_engine_manager(svc->instance) : nullptr;
    size_t count = 0;
    const char **engines = manager ? typio_engine_manager_list_by_type(manager, TYPIO_ENGINE_TYPE_KEYBOARD, &count) : nullptr;
    char *result = build_string_array(engines, count);
    free(engines);
    return result;
}

static char *build_available_engines(TypioStatusService *svc)
{
    TypioEngineManager *manager = svc ? typio_instance_get_engine_manager(svc->instance) : nullptr;
    size_t count = 0;
    const char **engines = manager ? typio_engine_manager_list(manager, &count) : nullptr;
    char *result = build_string_array(engines, count);
    return result;
}

static char *build_ordered_keyboard_engines(TypioStatusService *svc)
{
    TypioEngineManager *manager = svc ? typio_instance_get_engine_manager(svc->instance) : nullptr;
    size_t count = 0;
    const char **engines = manager ? typio_engine_manager_list_ordered_keyboards(manager, &count) : nullptr;
    char *result = build_string_array(engines, count);
    return result;
}

static char *build_ordered_engines(TypioStatusService *svc)
{
    /* Same as ordered keyboard engines for now */
    return build_ordered_keyboard_engines(svc);
}

static char *build_engine_display_names(TypioStatusService *svc)
{
    TypioEngineManager *manager = svc ? typio_instance_get_engine_manager(svc->instance) : nullptr;
    size_t count = 0;
    const char **engines = manager ? typio_engine_manager_list(manager, &count) : nullptr;
    TipJsonBuilder *b = tip_json_builder_new();
    int first = 1;

    TIP_JSON_OBJ_START(b);
    for (size_t i = 0; i < count; i++) {
        const TypioEngineInfo *info = typio_engine_manager_get_info(manager, engines[i]);
        const char *display = typio_engine_label_from_info(info);
        if (!display || !*display)
            continue;
        if (!first) TIP_JSON_COMMA(b);
        first = 0;
        TIP_JSON_KEY(b, engines[i]);
        tip_json_builder_append_string(b, display);
    }
    TIP_JSON_OBJ_END(b);
    return tip_json_builder_steal(b);
}

static char *build_engine_order(TypioStatusService *svc)
{
    TypioConfig *config = svc && svc->instance ? typio_instance_get_config(svc->instance) : nullptr;
    size_t count = config ? typio_config_get_array_size(config, "engine_order") : 0;
    TipJsonBuilder *b = tip_json_builder_new();
    TIP_JSON_ARR_START(b);
    for (size_t i = 0; i < count; i++) {
        const char *name = typio_config_get_array_string(config, "engine_order", i);
        if (!name)
            continue;
        if (i > 0) TIP_JSON_COMMA(b);
        tip_json_builder_append_string(b, name);
    }
    TIP_JSON_ARR_END(b);
    return tip_json_builder_steal(b);
}

static char *build_available_voice_engines(TypioStatusService *svc)
{
    TypioEngineManager *manager = svc ? typio_instance_get_engine_manager(svc->instance) : nullptr;
    size_t count = 0;
    const char **engines = manager ? typio_engine_manager_list_by_type(manager, TYPIO_ENGINE_TYPE_VOICE, &count) : nullptr;
    char *result = build_string_array(engines, count);
    free(engines);
    return result;
}

static char *build_active_engine_state(TypioStatusService *svc)
{
    TypioEngine *engine = svc_active_keyboard_engine(svc);
    const TypioEngineInfo *info = engine ? engine->info : nullptr;
    const char *config_path = engine ? typio_engine_get_config_path(engine) : nullptr;
    const char *engine_name = engine ? typio_engine_get_name(engine) : nullptr;
    TipJsonBuilder *b = tip_json_builder_new();
    int first = 1;

    TIP_JSON_OBJ_START(b);
    if (info) {
        TIP_JSON_KEY(b, "name");
        tip_json_builder_append_string(b, info->name);
        TIP_JSON_COMMA(b);
        TIP_JSON_KEY(b, "display_name");
        tip_json_builder_append_string(b, info->display_name);
        TIP_JSON_COMMA(b);
        TIP_JSON_KEY(b, "icon");
        tip_json_builder_append_string(b, info->icon);
        TIP_JSON_COMMA(b);
        TIP_JSON_KEY(b, "language");
        tip_json_builder_append_string(b, info->language);
        TIP_JSON_COMMA(b);
        TIP_JSON_KEY(b, "engine_type");
        tip_json_builder_append_string(b, engine_type_name(info->type));
        TIP_JSON_COMMA(b);
        TIP_JSON_KEY(b, "capabilities");
        tip_json_builder_append_uint32(b, info->capabilities);
    }
    if (engine) {
        if (!first) TIP_JSON_COMMA(b);
        TIP_JSON_KEY(b, "active");
        tip_json_builder_append_bool(b, typio_engine_is_active(engine));
    }
    if (config_path && *config_path) {
        if (!first) TIP_JSON_COMMA(b);
        TIP_JSON_KEY(b, "config_path");
        tip_json_builder_append_string(b, config_path);
    }
    if (svc && svc->instance) {
        const TypioEngineMode *mode = typio_instance_get_last_mode(svc->instance);
        if (mode) {
            if (!first) TIP_JSON_COMMA(b);
            TIP_JSON_KEY(b, "mode_class");
            tip_json_builder_append_string(b, mode_class_name(mode->mode_class));
            TIP_JSON_COMMA(b);
            TIP_JSON_KEY(b, "mode_id");
            tip_json_builder_append_string(b, mode->mode_id);
            TIP_JSON_COMMA(b);
            TIP_JSON_KEY(b, "mode_display_label");
            tip_json_builder_append_string(b, mode->display_label);
            TIP_JSON_COMMA(b);
            TIP_JSON_KEY(b, "mode_icon");
            tip_json_builder_append_string(b, mode->icon_name);
        }
    }
    if (svc && svc->instance && engine_name && *engine_name) {
        TypioConfig *config = typio_instance_get_engine_config(svc->instance, engine_name);
        if (config) {
            size_t ccount = typio_config_key_count(config);
            for (size_t i = 0; i < ccount; i++) {
                char *key = typio_config_key_at(config, i);
                const TypioConfigValue *value = key ? typio_config_get(config, key) : nullptr;
                if (!value) {
                    free(key);
                    continue;
                }
                if (!first) TIP_JSON_COMMA(b);
                {
                    char *prefixed = calloc(strlen("config.") + strlen(key) + 1, sizeof(char));
                    if (prefixed) {
                        snprintf(prefixed, strlen("config.") + strlen(key) + 1, "config.%s", key);
                        TIP_JSON_KEY(b, prefixed);
                        free(prefixed);
                    } else {
                        TIP_JSON_KEY(b, key);
                    }
                }
                free(key);
                switch (value->type) {
                case TYPIO_CONFIG_STRING:
                    tip_json_builder_append_string(b, value->data.string_val);
                    break;
                case TYPIO_CONFIG_INT:
                    tip_json_builder_append_int(b, value->data.int_val);
                    break;
                case TYPIO_CONFIG_BOOL:
                    tip_json_builder_append_bool(b, value->data.bool_val);
                    break;
                case TYPIO_CONFIG_FLOAT:
                    tip_json_builder_append_double(b, value->data.float_val);
                    break;
                default:
                    tip_json_builder_append_null(b);
                    break;
                }
            }
            typio_config_free(config);
        }
    }
    TIP_JSON_OBJ_END(b);
    return tip_json_builder_steal(b);
}

static char *build_active_engine_mode(TypioStatusService *svc)
{
    const TypioEngineMode *mode = svc && svc->instance ? typio_instance_get_last_mode(svc->instance) : nullptr;
    TipJsonBuilder *b = tip_json_builder_new();

    TIP_JSON_OBJ_START(b);
    if (mode) {
        TIP_JSON_KEY(b, "mode_class");
        tip_json_builder_append_string(b, mode_class_name(mode->mode_class));
        TIP_JSON_COMMA(b);
        TIP_JSON_KEY(b, "mode_id");
        tip_json_builder_append_string(b, mode->mode_id);
        TIP_JSON_COMMA(b);
        TIP_JSON_KEY(b, "display_label");
        tip_json_builder_append_string(b, mode->display_label);
        TIP_JSON_COMMA(b);
        TIP_JSON_KEY(b, "icon_name");
        tip_json_builder_append_string(b, mode->icon_name);
    }
    TIP_JSON_OBJ_END(b);
    return tip_json_builder_steal(b);
}

static char *build_runtime_state(TypioStatusService *svc)
{
    TypioStatusRuntimeState state = {0};
    TipJsonBuilder *b = tip_json_builder_new();

    if (svc && svc->runtime_state_callback)
        svc->runtime_state_callback(svc->runtime_state_user_data, &state);

    TIP_JSON_OBJ_START(b);
    TIP_JSON_KEY(b, "frontend_backend");
    tip_json_builder_append_string(b, state.frontend_backend ? state.frontend_backend : "");
    TIP_JSON_COMMA(b);
    TIP_JSON_KEY(b, "lifecycle_phase");
    tip_json_builder_append_string(b, state.lifecycle_phase ? state.lifecycle_phase : "");
    TIP_JSON_COMMA(b);
    TIP_JSON_KEY(b, "virtual_keyboard_state");
    tip_json_builder_append_string(b, state.virtual_keyboard_state ? state.virtual_keyboard_state : "");
    TIP_JSON_COMMA(b);
    TIP_JSON_KEY(b, "keyboard_grab_active");
    tip_json_builder_append_bool(b, state.keyboard_grab_active);
    TIP_JSON_COMMA(b);
    TIP_JSON_KEY(b, "virtual_keyboard_has_keymap");
    tip_json_builder_append_bool(b, state.virtual_keyboard_has_keymap);
    TIP_JSON_COMMA(b);
    TIP_JSON_KEY(b, "watchdog_armed");
    tip_json_builder_append_bool(b, state.watchdog_armed);
    TIP_JSON_COMMA(b);
    TIP_JSON_KEY(b, "active_key_generation");
    tip_json_builder_append_uint32(b, state.active_key_generation);
    TIP_JSON_COMMA(b);
    TIP_JSON_KEY(b, "virtual_keyboard_keymap_generation");
    tip_json_builder_append_uint32(b, state.virtual_keyboard_keymap_generation);
    TIP_JSON_COMMA(b);
    TIP_JSON_KEY(b, "virtual_keyboard_drop_count");
    tip_json_builder_append_uint32(b, state.virtual_keyboard_drop_count);
    TIP_JSON_COMMA(b);
    TIP_JSON_KEY(b, "virtual_keyboard_state_age_ms");
    tip_json_builder_append_uint32(b, state.virtual_keyboard_state_age_ms);
    TIP_JSON_COMMA(b);
    TIP_JSON_KEY(b, "virtual_keyboard_keymap_age_ms");
    tip_json_builder_append_uint32(b, state.virtual_keyboard_keymap_age_ms);
    TIP_JSON_COMMA(b);
    TIP_JSON_KEY(b, "virtual_keyboard_forward_age_ms");
    tip_json_builder_append_uint32(b, state.virtual_keyboard_forward_age_ms);
    TIP_JSON_COMMA(b);
    TIP_JSON_KEY(b, "virtual_keyboard_keymap_deadline_remaining_ms");
    tip_json_builder_append_int32(b, state.virtual_keyboard_keymap_deadline_remaining_ms);
    TIP_JSON_OBJ_END(b);
    return tip_json_builder_steal(b);
}

static char *build_rime_schema(TypioStatusService *svc)
{
    char *schema = svc && svc->instance ? typio_instance_dup_rime_schema(svc->instance) : nullptr;
    TipJsonBuilder *b = tip_json_builder_new();
    tip_json_builder_append_string(b, schema ? schema : "");
    free(schema);
    return tip_json_builder_steal(b);
}

static char *build_config_text(TypioStatusService *svc)
{
    char *text = svc && svc->instance ? typio_instance_get_config_text(svc->instance) : nullptr;
    TipJsonBuilder *b = tip_json_builder_new();
    tip_json_builder_append_string(b, text ? text : "");
    free(text);
    return tip_json_builder_steal(b);
}

/* ------------------------------------------------------------------ */
/*  Property dispatch                                                 */
/* ------------------------------------------------------------------ */

static char *dispatch_property(TypioStatusService *svc, const char *property)
{
    if (strcmp(property, TYPIO_IPC_PROP_VERSION) == 0)
        return build_version(svc);
    if (strcmp(property, TYPIO_IPC_PROP_ACTIVE_KEYBOARD_ENGINE) == 0 ||
        strcmp(property, TYPIO_IPC_PROP_ACTIVE_ENGINE) == 0)
        return build_active_engine(svc);
    if (strcmp(property, TYPIO_IPC_PROP_AVAILABLE_KEYBOARD_ENGINES) == 0)
        return build_available_keyboard_engines(svc);
    if (strcmp(property, TYPIO_IPC_PROP_AVAILABLE_ENGINES) == 0)
        return build_available_engines(svc);
    if (strcmp(property, TYPIO_IPC_PROP_ORDERED_KEYBOARD_ENGINES) == 0 ||
        strcmp(property, TYPIO_IPC_PROP_ORDERED_ENGINES) == 0)
        return build_ordered_engines(svc);
    if (strcmp(property, TYPIO_IPC_PROP_ENGINE_DISPLAY_NAMES) == 0)
        return build_engine_display_names(svc);
    if (strcmp(property, TYPIO_IPC_PROP_ENGINE_ORDER) == 0)
        return build_engine_order(svc);
    if (strcmp(property, TYPIO_IPC_PROP_AVAILABLE_VOICE_ENGINES) == 0)
        return build_available_voice_engines(svc);
    if (strcmp(property, TYPIO_IPC_PROP_ACTIVE_VOICE_ENGINE) == 0)
        return build_active_voice_engine(svc);
    if (strcmp(property, TYPIO_IPC_PROP_ACTIVE_ENGINE_STATE) == 0)
        return build_active_engine_state(svc);
    if (strcmp(property, TYPIO_IPC_PROP_ACTIVE_ENGINE_MODE) == 0)
        return build_active_engine_mode(svc);
    if (strcmp(property, TYPIO_IPC_PROP_RUNTIME_STATE) == 0)
        return build_runtime_state(svc);
    if (strcmp(property, TYPIO_IPC_PROP_RIME_SCHEMA) == 0)
        return build_rime_schema(svc);
    if (strcmp(property, TYPIO_IPC_PROP_CONFIG_TEXT) == 0)
        return build_config_text(svc);
    return nullptr;
}

/* ------------------------------------------------------------------ */
/*  Method handlers                                                   */
/* ------------------------------------------------------------------ */

static char *handle_get(TypioStatusService *svc, const char *params_json, int id)
{
    char *property = nullptr;
    char *value = nullptr;
    char *result = nullptr;

    if (!params_json)
        return tip_json_build_error(0, -32602, "Invalid params");

    property = tip_json_extract_string(params_json, "property");
    if (!property) {
        return tip_json_build_error(id, -32602, "Missing 'property' param");
    }

    value = dispatch_property(svc, property);
    free(property);
    if (!value) {
        return tip_json_build_error(id, -32602, "Unknown property");
    }

    result = value; /* value is already JSON */
    return result;
}

static char *handle_getall(TypioStatusService *svc, [[maybe_unused]] const char *params_json, [[maybe_unused]] int id)
{
    return typio_status_service_get_all_json(svc);
}

static char *handle_activate_engine(TypioStatusService *svc, const char *params_json, int id)
{
    TypioEngineManager *manager;
    TypioResult result;
    char *engine_name = nullptr;

    engine_name = tip_json_extract_string(params_json, "engine");
    if (!engine_name || !*engine_name) {
        return tip_json_build_error(id, -32602, "Missing 'engine' param");
    }

    manager = svc ? typio_instance_get_engine_manager(svc->instance) : nullptr;
    if (!manager) {
        free(engine_name);
        return tip_json_build_error(id, -32603, "Engine manager not available");
    }

    result = typio_engine_manager_set_active(manager, engine_name);
    if (result != TYPIO_OK) {
        free(engine_name);
        return tip_json_build_error(id, -32603, "Failed to activate engine");
    }

    if (svc && svc->instance) {
        TypioConfig *config = typio_instance_get_config(svc->instance);
        if (config) {
            typio_config_set_string(config, "default_engine", engine_name);
            typio_instance_save_config(svc->instance);
        }
    }

    free(engine_name);
    return tip_json_build_response(id, "null");
}

static char *handle_next_engine(TypioStatusService *svc, const char *params_json, int id)
{
    TypioEngineManager *manager;
    TypioResult result;

    manager = svc ? typio_instance_get_engine_manager(svc->instance) : nullptr;
    if (!manager)
        return tip_json_build_error(id, -32603, "Engine manager not available");

    result = typio_engine_manager_next(manager);
    if (result != TYPIO_OK)
        return tip_json_build_error(id, -32603, "Failed to switch engine");

    return tip_json_build_response(id, "null");
}

static char *handle_reload_config(TypioStatusService *svc, const char *params_json, int id)
{
    TypioResult result;

    result = svc ? typio_instance_reload_config(svc->instance) : TYPIO_ERROR;
    if (result != TYPIO_OK)
        return tip_json_build_error(id, -32603, "Failed to reload config");

    return tip_json_build_response(id, "null");
}

static char *handle_stop(TypioStatusService *svc, const char *params_json, int id)
{
    if (!svc || !svc->stop_callback)
        return tip_json_build_error(id, -32603, "Stop callback not available");

    svc->stop_callback(svc->stop_user_data);
    return tip_json_build_response(id, "null");
}

static char *handle_set_config_text(TypioStatusService *svc, const char *params_json, int id)
{
    TypioResult result;
    char *text = nullptr;

    text = tip_json_extract_string(params_json, "content");
    if (!text)
        return tip_json_build_error(id, -32602, "Missing 'content' param");

    result = svc ? typio_instance_set_config_text(svc->instance, text) : TYPIO_ERROR;
    free(text);
    if (result != TYPIO_OK)
        return tip_json_build_error(id, -32603, "Failed to save config");

    return tip_json_build_response(id, "null");
}

static char *handle_set_rime_schema(TypioStatusService *svc, const char *params_json, int id)
{
    TypioResult result;
    char *schema = nullptr;

    schema = tip_json_extract_string(params_json, "schema");
    if (!schema)
        return tip_json_build_error(id, -32602, "Missing 'schema' param");

    result = svc ? typio_instance_set_rime_schema(svc->instance, schema) : TYPIO_ERROR;
    if (result != TYPIO_OK) {
        free(schema);
        return tip_json_build_error(id, -32603, "Failed to persist schema");
    }

    result = svc ? typio_instance_reload_config(svc->instance) : TYPIO_ERROR;
    free(schema);
    if (result != TYPIO_OK)
        return tip_json_build_error(id, -32603, "Failed to reload after schema change");

    return tip_json_build_response(id, "null");
}

static char *handle_deploy_rime_config(TypioStatusService *svc, const char *params_json, int id)
{
    TypioResult result;

    result = svc ? typio_instance_deploy_rime_config(svc->instance) : TYPIO_ERROR;
    if (result != TYPIO_OK)
        return tip_json_build_error(id, -32603, "Failed to deploy Rime config");

    return tip_json_build_response(id, "null");
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

TypioStatusService *typio_status_service_new(TypioInstance *instance)
{
    TypioStatusService *svc;

    if (!instance)
        return nullptr;

    svc = calloc(1, sizeof(*svc));
    if (!svc)
        return nullptr;

    svc->instance = instance;
    return svc;
}

void typio_status_service_destroy(TypioStatusService *svc)
{
    free(svc);
}

char *typio_status_service_handle(TypioStatusService *svc,
                                   const char *method,
                                   const char *params_json,
                                   int id)
{
    if (!svc || !method)
        return nullptr;

    if (strcmp(method, TYPIO_IPC_METHOD_GETALL) == 0)
        return handle_getall(svc, params_json, id);
    if (strcmp(method, TYPIO_IPC_METHOD_GET) == 0)
        return handle_get(svc, params_json, id);
    if (strcmp(method, TYPIO_IPC_METHOD_ACTIVATE_ENGINE) == 0)
        return handle_activate_engine(svc, params_json, id);
    if (strcmp(method, TYPIO_IPC_METHOD_NEXT_ENGINE) == 0)
        return handle_next_engine(svc, params_json, id);
    if (strcmp(method, TYPIO_IPC_METHOD_RELOAD_CONFIG) == 0)
        return handle_reload_config(svc, params_json, id);
    if (strcmp(method, TYPIO_IPC_METHOD_STOP) == 0)
        return handle_stop(svc, params_json, id);
    if (strcmp(method, TYPIO_IPC_METHOD_SET_CONFIG_TEXT) == 0)
        return handle_set_config_text(svc, params_json, id);
    if (strcmp(method, TYPIO_IPC_METHOD_SET_RIME_SCHEMA) == 0)
        return handle_set_rime_schema(svc, params_json, id);
    if (strcmp(method, TYPIO_IPC_METHOD_DEPLOY_RIME) == 0)
        return handle_deploy_rime_config(svc, params_json, id);

    return tip_json_build_error(id, -32601, "Method not found");
}

char *typio_status_service_get_property_json(TypioStatusService *svc,
                                              const char *property)
{
    if (!svc || !property)
        return nullptr;
    return dispatch_property(svc, property);
}

char *typio_status_service_get_all_json(TypioStatusService *svc)
{
    TipJsonBuilder *b;
    const char *properties[] = {
        TYPIO_IPC_PROP_VERSION,
        TYPIO_IPC_PROP_ACTIVE_KEYBOARD_ENGINE,
        TYPIO_IPC_PROP_ACTIVE_ENGINE,
        TYPIO_IPC_PROP_AVAILABLE_KEYBOARD_ENGINES,
        TYPIO_IPC_PROP_AVAILABLE_ENGINES,
        TYPIO_IPC_PROP_ORDERED_KEYBOARD_ENGINES,
        TYPIO_IPC_PROP_ORDERED_ENGINES,
        TYPIO_IPC_PROP_ENGINE_DISPLAY_NAMES,
        TYPIO_IPC_PROP_ENGINE_ORDER,
        TYPIO_IPC_PROP_AVAILABLE_VOICE_ENGINES,
        TYPIO_IPC_PROP_ACTIVE_VOICE_ENGINE,
        TYPIO_IPC_PROP_ACTIVE_ENGINE_STATE,
        TYPIO_IPC_PROP_ACTIVE_ENGINE_MODE,
        TYPIO_IPC_PROP_RUNTIME_STATE,
        TYPIO_IPC_PROP_RIME_SCHEMA,
        TYPIO_IPC_PROP_CONFIG_TEXT,
    };

    if (!svc)
        return nullptr;

    b = tip_json_builder_new();
    TIP_JSON_OBJ_START(b);
    for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]); i++) {
        char *value = dispatch_property(svc, properties[i]);
        if (i > 0) TIP_JSON_COMMA(b);
        TIP_JSON_KEY(b, properties[i]);
        if (value) {
            tip_json_builder_append_raw(b, value);
            free(value);
        } else {
            tip_json_builder_append_null(b);
        }
    }
    TIP_JSON_OBJ_END(b);
    return tip_json_builder_steal(b);
}

char *typio_status_service_get_changed_json(TypioStatusService *svc)
{
    /* Same as get_all for now — all mutable properties are emitted */
    return typio_status_service_get_all_json(svc);
}

void typio_status_service_set_stop_callback(TypioStatusService *svc,
                                             TypioStatusServiceStopCallback callback,
                                             void *user_data)
{
    if (!svc)
        return;
    svc->stop_callback = callback;
    svc->stop_user_data = user_data;
}

void typio_status_service_set_runtime_state_callback(
    TypioStatusService *svc,
    TypioStatusServiceRuntimeStateCallback callback,
    void *user_data)
{
    if (!svc)
        return;
    svc->runtime_state_callback = callback;
    svc->runtime_state_user_data = user_data;
}

/* ------------------------------------------------------------------ */
/*  State controller integration                                      */
/* ------------------------------------------------------------------ */

static void svc_state_change_callback(void *user_data,
                                       [[maybe_unused]] TypioStateChangeType change_type)
{
    TypioStatusService *svc = user_data;
    (void)svc;
    /* The transport layer (IpcBus or StatusBus) listens to the
     * state controller and calls emit_properties_changed. */
}

void typio_status_service_bind_state_controller(TypioStatusService *svc,
                                                 TypioStateController *ctrl)
{
    if (!svc)
        return;
    if (svc->state_controller && svc->state_controller != ctrl)
        typio_state_controller_remove_listener(svc->state_controller, svc);
    svc->state_controller = ctrl;
    if (ctrl) {
        typio_state_controller_add_listener(
            ctrl,
            (TypioStateListener){ .user_data = svc,
                                  .callback = svc_state_change_callback });
    }
}
