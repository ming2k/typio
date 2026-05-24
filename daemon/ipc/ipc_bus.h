/**
 * @file ipc_bus.h
 * @brief Typio IPC Bus — UDS transport for daemon control surface.
 */

#ifndef TYPIO_IPC_BUS_H
#define TYPIO_IPC_BUS_H

#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct TypioStateController;
typedef struct TypioIpcBus TypioIpcBus;

typedef void (*TypioIpcBusStopCallback)(void *user_data);
typedef struct TypioIpcBusRuntimeState {
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
} TypioIpcBusRuntimeState;

typedef void (*TypioIpcBusRuntimeStateCallback)(void *user_data,
                                                 TypioIpcBusRuntimeState *state);

TypioIpcBus *typio_ipc_bus_new(TypioInstance *instance);
void typio_ipc_bus_destroy(TypioIpcBus *bus);
int  typio_ipc_bus_get_fd(TypioIpcBus *bus);
void typio_ipc_bus_dispatch(TypioIpcBus *bus);
void typio_ipc_bus_emit_properties_changed(TypioIpcBus *bus);
void typio_ipc_bus_set_runtime_state_callback(TypioIpcBus *bus,
                                               TypioIpcBusRuntimeStateCallback callback,
                                               void *user_data);
void typio_ipc_bus_set_stop_callback(TypioIpcBus *bus,
                                      TypioIpcBusStopCallback callback,
                                      void *user_data);
void typio_ipc_bus_bind_state_controller(TypioIpcBus *bus,
                                          struct TypioStateController *ctrl);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_IPC_BUS_H */
