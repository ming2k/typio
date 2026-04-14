/**
 * @file status.h
 * @brief D-Bus status interface for structured Typio state
 */

#ifndef TYPIO_STATUS_H
#define TYPIO_STATUS_H

#include "typio/dbus_protocol.h"
#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioStatusBus TypioStatusBus;
typedef void (*TypioStatusBusStopCallback)(void *user_data);
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
typedef void (*TypioStatusBusRuntimeStateCallback)(void *user_data,
                                                   TypioStatusRuntimeState *state);

TypioStatusBus *typio_status_bus_new(TypioInstance *instance);
void typio_status_bus_destroy(TypioStatusBus *bus);
int typio_status_bus_get_fd(TypioStatusBus *bus);
int typio_status_bus_dispatch(TypioStatusBus *bus);
void typio_status_bus_emit_properties_changed(TypioStatusBus *bus);
void typio_status_bus_set_runtime_state_callback(
    TypioStatusBus *bus,
    TypioStatusBusRuntimeStateCallback callback,
    void *user_data);
void typio_status_bus_set_stop_callback(TypioStatusBus *bus,
                                        TypioStatusBusStopCallback callback,
                                        void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_STATUS_H */
