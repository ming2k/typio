/**
 * @file uds_server.h
 * @brief Unix Domain Socket server for Typio IPC.
 *
 * Multiplexes multiple client connections via epoll(7).
 * The epoll fd itself can be watched by poll() / select(),
 * which is how the Wayland event loop consumes it.
 */

#ifndef TYPIO_UDS_SERVER_H
#define TYPIO_UDS_SERVER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioUdsServer TypioUdsServer;

/**
 * @brief Handler called for each complete JSON-RPC request.
 *
 * @param json_request  NUL-terminated JSON payload (no length prefix).
 * @param user_data     Opaque pointer passed to set_handler().
 * @return malloc'd JSON response string, or NULL to send no reply
 *         (e.g. for malformed requests the server auto-replies).
 */
typedef char *(*TypioUdsRequestHandler)(const char *json_request,
                                         void *user_data);

/**
 * @brief Create and bind a UDS listener.
 *
 * Stale sockets are detected with a connect() probe and unlinked
 * before bind().  The socket mode is set to 0600.
 *
 * @param socket_path  Absolute filesystem path.
 * @return Server handle, or NULL on error.
 */
TypioUdsServer *typio_uds_server_new(const char *socket_path);

void typio_uds_server_destroy(TypioUdsServer *srv);

/**
 * @brief Return the epoll fd used to watch all sockets.
 *
 * Suitable for poll()/select() in the Wayland event loop.
 */
int typio_uds_server_get_fd(TypioUdsServer *srv);

/**
 * @brief Process all ready sockets (accept, read, write).
 *
 * Should be called whenever get_fd() reports readable.
 * Internally uses epoll_wait(timeout=0) so it is non-blocking.
 */
void typio_uds_server_dispatch(TypioUdsServer *srv);

void typio_uds_server_set_handler(TypioUdsServer *srv,
                                   TypioUdsRequestHandler handler,
                                   void *user_data);

/**
 * @brief Send a JSON notification to every connected client.
 *
 * The payload is framed with a 4-byte big-endian length header.
 */
void typio_uds_server_broadcast(TypioUdsServer *srv,
                                 const char *json_notification);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_UDS_SERVER_H */
