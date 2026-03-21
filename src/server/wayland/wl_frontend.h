/**
 * @file wl_frontend.h
 * @brief Wayland input method frontend public interface
 *
 * This module implements the zwp_input_method_v2 side of the Wayland
 * text-input/input-method stack, allowing Typio to act as an input method
 * in sessions where applications and the compositor provide text-input-v3.
 */

#ifndef TYPIO_WL_FRONTEND_H
#define TYPIO_WL_FRONTEND_H

#include "typio/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque Wayland frontend structure
 */
typedef struct TypioWlFrontend TypioWlFrontend;

/**
 * @brief Wayland frontend configuration
 */
typedef struct TypioWlFrontendConfig {
    const char *display_name;   /* Wayland display name (nullptr for default) */
} TypioWlFrontendConfig;

/**
 * @brief Create a new Wayland frontend
 * @param instance Typio instance to connect to
 * @param config Optional configuration (nullptr for defaults)
 * @return New frontend or nullptr on failure
 */
TypioWlFrontend *typio_wl_frontend_new(TypioInstance *instance,
                                        const TypioWlFrontendConfig *config);

/**
 * @brief Run the Wayland event loop
 * @param frontend Wayland frontend
 * @return 0 on clean shutdown, -1 on error
 *
 * This function blocks and processes Wayland events until
 * typio_wl_frontend_stop() is called or an error occurs.
 */
int typio_wl_frontend_run(TypioWlFrontend *frontend);

/**
 * @brief Stop the Wayland event loop
 * @param frontend Wayland frontend
 *
 * Thread-safe. Can be called from a signal handler.
 */
void typio_wl_frontend_stop(TypioWlFrontend *frontend);

/**
 * @brief Check if frontend is running
 * @param frontend Wayland frontend
 * @return true if the event loop is running
 */
bool typio_wl_frontend_is_running(TypioWlFrontend *frontend);

/**
 * @brief Destroy the Wayland frontend
 * @param frontend Wayland frontend to destroy
 */
void typio_wl_frontend_destroy(TypioWlFrontend *frontend);

/**
 * @brief Get the last error message
 * @param frontend Wayland frontend
 * @return Error message or nullptr if no error
 */
const char *typio_wl_frontend_get_error(TypioWlFrontend *frontend);

/**
 * @brief Set the system tray to integrate with the event loop
 * @param frontend Wayland frontend
 * @param tray System tray (can be nullptr to disable)
 *
 * The tray's D-Bus events will be processed alongside Wayland events.
 */
void typio_wl_frontend_set_tray(TypioWlFrontend *frontend, void *tray);
void typio_wl_frontend_set_status_bus(TypioWlFrontend *frontend, void *status_bus);
void typio_wl_frontend_remember_active_engine(TypioWlFrontend *frontend,
                                              const char *engine_name);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_FRONTEND_H */
