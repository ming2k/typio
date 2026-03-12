/**
 * @file tray.h
 * @brief System tray (StatusNotifierItem) public interface
 *
 * Implements the org.kde.StatusNotifierItem D-Bus protocol for
 * displaying a system tray icon on Wayland compositors.
 */

#ifndef TYPIO_TRAY_H
#define TYPIO_TRAY_H

#include "typio/types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque tray structure
 */
typedef struct TypioTray TypioTray;

/**
 * @brief Tray icon status
 */
typedef enum {
    TYPIO_TRAY_STATUS_PASSIVE,      /* Normal, not urgent */
    TYPIO_TRAY_STATUS_ACTIVE,       /* Input method active */
    TYPIO_TRAY_STATUS_NEEDS_ATTENTION, /* Needs user attention */
} TypioTrayStatus;

/**
 * @brief Callback for tray menu activation
 */
typedef void (*TypioTrayMenuCallback)(TypioTray *tray, const char *action,
                                      void *user_data);

/**
 * @brief Tray configuration
 */
typedef struct TypioTrayConfig {
    const char *icon_name;          /* Icon name (freedesktop icon theme) */
    const char *tooltip;            /* Tooltip text */
    TypioTrayMenuCallback menu_callback;
    void *user_data;
} TypioTrayConfig;

/**
 * @brief Create a new system tray
 * @param instance Typio instance
 * @param config Optional configuration (NULL for defaults)
 * @return New tray or NULL on failure
 */
TypioTray *typio_tray_new(TypioInstance *instance, const TypioTrayConfig *config);

/**
 * @brief Destroy the system tray
 * @param tray Tray to destroy
 */
void typio_tray_destroy(TypioTray *tray);

/**
 * @brief Get the D-Bus file descriptor for event loop integration
 * @param tray System tray
 * @return File descriptor or -1 if not available
 */
int typio_tray_get_fd(TypioTray *tray);

/**
 * @brief Process pending D-Bus events
 * @param tray System tray
 * @return 0 on success, -1 on error
 *
 * Call this when the D-Bus fd is readable.
 */
int typio_tray_dispatch(TypioTray *tray);

/**
 * @brief Set the tray status
 * @param tray System tray
 * @param status New status
 */
void typio_tray_set_status(TypioTray *tray, TypioTrayStatus status);

/**
 * @brief Set the tray icon
 * @param tray System tray
 * @param icon_name Icon name from freedesktop icon theme
 */
void typio_tray_set_icon(TypioTray *tray, const char *icon_name);

/**
 * @brief Set the tray tooltip
 * @param tray System tray
 * @param title Tooltip title
 * @param description Tooltip description (can be NULL)
 */
void typio_tray_set_tooltip(TypioTray *tray, const char *title,
                            const char *description);

/**
 * @brief Update the engine display in the tray
 * @param tray System tray
 * @param engine_name Current engine name (or NULL if none)
 * @param is_active Whether input method is active
 */
void typio_tray_update_engine(TypioTray *tray, const char *engine_name,
                              bool is_active);

/**
 * @brief Check if tray is registered with the system
 * @param tray System tray
 * @return true if registered and visible
 */
bool typio_tray_is_registered(TypioTray *tray);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_TRAY_H */
