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

TypioStatusBus *typio_status_bus_new(TypioInstance *instance);
void typio_status_bus_destroy(TypioStatusBus *bus);
int typio_status_bus_get_fd(TypioStatusBus *bus);
int typio_status_bus_dispatch(TypioStatusBus *bus);
void typio_status_bus_emit_properties_changed(TypioStatusBus *bus);
void typio_status_bus_set_stop_callback(TypioStatusBus *bus,
                                        TypioStatusBusStopCallback callback,
                                        void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_STATUS_H */
