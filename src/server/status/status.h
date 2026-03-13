/**
 * @file status.h
 * @brief D-Bus status interface for structured Typio state
 */

#ifndef TYPIO_STATUS_H
#define TYPIO_STATUS_H

#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TYPIO_STATUS_DBUS_SERVICE "org.typio.InputMethod1"
#define TYPIO_STATUS_DBUS_PATH "/org/typio/InputMethod1"
#define TYPIO_STATUS_DBUS_INTERFACE "org.typio.InputMethod1"

typedef struct TypioStatusBus TypioStatusBus;

TypioStatusBus *typio_status_bus_new(TypioInstance *instance);
void typio_status_bus_destroy(TypioStatusBus *bus);
int typio_status_bus_get_fd(TypioStatusBus *bus);
int typio_status_bus_dispatch(TypioStatusBus *bus);
void typio_status_bus_emit_properties_changed(TypioStatusBus *bus);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_STATUS_H */
