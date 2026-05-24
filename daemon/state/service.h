/**
 * @file status_service.h
 * @brief Transport-agnostic status & control service.
 *
 * Encapsulates all business logic previously embedded in
 * status/status.c.  Produces and consumes JSON strings,
 * with no knowledge of D-Bus or UDS.
 */

#ifndef TYPIO_STATUS_SERVICE_H
#define TYPIO_STATUS_SERVICE_H

#include "state/controller.h"
#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioStatusService TypioStatusService;

typedef void (*TypioStatusServiceStopCallback)(void *user_data);
typedef struct TypioStatusRuntimeState {
    const char *frontend_backend;
    const char *lifecycle_phase;
    const char *virtual_keyboard_state;
    bool keyboard_grab_active;
    bool virtual_keyboard_has_keymap;
    bool watchdog_armed;
    uint32_t active_key_generation;
    uint32_t virtual_keyboard_keymap_generation;
    uint32_t virtual_keyboard_drop_count;
    uint32_t virtual_keyboard_state_age_ms;
    uint32_t virtual_keyboard_keymap_age_ms;
    uint32_t virtual_keyboard_forward_age_ms;
    int32_t virtual_keyboard_keymap_deadline_remaining_ms;
} TypioStatusRuntimeState;

typedef void (*TypioStatusServiceRuntimeStateCallback)(void *user_data,
                                                        TypioStatusRuntimeState *state);

TypioStatusService *typio_status_service_new(TypioInstance *instance);
void typio_status_service_destroy(TypioStatusService *svc);

/**
 * @brief Handle a JSON-RPC method call.
 *
 * @param method      Method name (e.g. "GetAll", "ActivateEngine").
 * @param params_json JSON object string for params, or NULL.
 * @param id          JSON-RPC request id (0 if unknown).
 * @return malloc'd JSON result string, or NULL on error.
 *         Caller must free().
 */
char *typio_status_service_handle(TypioStatusService *svc,
                                   const char *method,
                                   const char *params_json,
                                   int id);

/**
 * @brief Get a single property as JSON value.
 * @return malloc'd JSON value string, or NULL if unknown.
 */
char *typio_status_service_get_property_json(TypioStatusService *svc,
                                              const char *property);

/**
 * @brief Get all properties as a JSON object.
 * @return malloc'd JSON object string.  Caller frees.
 */
char *typio_status_service_get_all_json(TypioStatusService *svc);

/**
 * @brief Build a JSON object suitable for the PropertiesChanged
 *        notification (only changed properties).
 */
char *typio_status_service_get_changed_json(TypioStatusService *svc);

void typio_status_service_set_stop_callback(TypioStatusService *svc,
                                             TypioStatusServiceStopCallback callback,
                                             void *user_data);
void typio_status_service_set_runtime_state_callback(
    TypioStatusService *svc,
    TypioStatusServiceRuntimeStateCallback callback,
    void *user_data);
void typio_status_service_bind_state_controller(TypioStatusService *svc,
                                                 TypioStateController *ctrl);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_STATUS_SERVICE_H */
