/**
 * @file ipc_bus.c
 * @brief Glue UDS server ↔ StatusService for daemon IPC.
 */

#include "ipc_bus.h"
#include "state/service.h"
#include "tip_json.h"
#include "tip_protocol.h"
#include "uds_server.h"

#include "typio/log.h"
#include "typio/string.h"

#include <stdlib.h>
#include <string.h>

struct TypioIpcBus {
    TypioStatusService *service;
    TypioUdsServer *uds;
};

/* ------------------------------------------------------------------ */
/*  Request handler (called by UDS server)                            */
/* ------------------------------------------------------------------ */

static char *ipc_bus_handle_request(const char *json_request, void *user_data)
{
    TypioIpcBus *bus = user_data;
    char *method = nullptr;
    const char *params_start = nullptr;
    size_t params_len = 0;
    char *params = nullptr;
    char *response = nullptr;
    int id = 0;

    if (!bus || !bus->service)
        return tip_json_build_error(0, -32603, "Internal error");

    tip_json_extract_id(json_request, &id);
    method = tip_json_extract_string(json_request, "method");
    if (!method) {
        return tip_json_build_error(id, -32600, "Invalid Request: missing method");
    }

    if (tip_json_extract_params(json_request, &params_start, &params_len)) {
        if (params_start && params_len > 0) {
            params = malloc(params_len + 1);
            if (params) {
                memcpy(params, params_start, params_len);
                params[params_len] = '\0';
            }
        }
    }

    response = typio_status_service_handle(bus->service, method, params, id);
    free(method);
    free(params);

    if (!response) {
        return tip_json_build_error(id, -32603, "Internal error");
    }

    /* Wrap bare JSON results into JSON-RPC envelope */
    if (!strstr(response, "\"jsonrpc\"")) {
        char *wrapped = tip_json_build_response(id, response);
        free(response);
        response = wrapped;
    }

    return response;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

TypioIpcBus *typio_ipc_bus_new(TypioInstance *instance)
{
    TypioIpcBus *bus;
    char *socket_path;

    if (!instance)
        return nullptr;

    bus = calloc(1, sizeof(*bus));
    if (!bus)
        return nullptr;

    bus->service = typio_status_service_new(instance);
    if (!bus->service) {
        free(bus);
        return nullptr;
    }

    socket_path = typio_ipc_socket_path();
    if (!socket_path) {
        typio_status_service_destroy(bus->service);
        free(bus);
        return nullptr;
    }

    bus->uds = typio_uds_server_new(socket_path);
    free(socket_path);
    if (!bus->uds) {
        typio_status_service_destroy(bus->service);
        free(bus);
        return nullptr;
    }

    typio_uds_server_set_handler(bus->uds, ipc_bus_handle_request, bus);
    typio_log(TYPIO_LOG_INFO, "Typio IPC bus initialized");
    return bus;
}

void typio_ipc_bus_destroy(TypioIpcBus *bus)
{
    if (!bus)
        return;
    if (bus->uds)
        typio_uds_server_destroy(bus->uds);
    if (bus->service)
        typio_status_service_destroy(bus->service);
    free(bus);
}

int typio_ipc_bus_get_fd(TypioIpcBus *bus)
{
    return bus ? typio_uds_server_get_fd(bus->uds) : -1;
}

void typio_ipc_bus_dispatch(TypioIpcBus *bus)
{
    if (!bus || !bus->uds)
        return;
    typio_uds_server_dispatch(bus->uds);
}

void typio_ipc_bus_emit_properties_changed(TypioIpcBus *bus)
{
    char *changed_json;
    char *notify;

    if (!bus || !bus->uds || !bus->service)
        return;

    changed_json = typio_status_service_get_changed_json(bus->service);
    if (!changed_json)
        return;

    notify = tip_json_build_notify("PropertiesChanged", changed_json);
    if (notify) {
        typio_uds_server_broadcast(bus->uds, notify);
        free(notify);
    }
    free(changed_json);
}

void typio_ipc_bus_set_runtime_state_callback(TypioIpcBus *bus,
                                               TypioIpcBusRuntimeStateCallback callback,
                                               void *user_data)
{
    if (!bus)
        return;
    /* Forward to status service with a small adapter if type sizes differ */
    typio_status_service_set_runtime_state_callback(
        bus->service,
        (TypioStatusServiceRuntimeStateCallback)callback,
        user_data);
}

void typio_ipc_bus_set_stop_callback(TypioIpcBus *bus,
                                      TypioIpcBusStopCallback callback,
                                      void *user_data)
{
    if (!bus)
        return;
    typio_status_service_set_stop_callback(
        bus->service,
        (TypioStatusServiceStopCallback)callback,
        user_data);
}

void typio_ipc_bus_bind_state_controller(TypioIpcBus *bus,
                                          struct TypioStateController *ctrl)
{
    if (!bus)
        return;
    typio_status_service_bind_state_controller(bus->service, ctrl);
}
