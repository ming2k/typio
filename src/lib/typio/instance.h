/**
 * @file instance.h
 * @brief Main Typio instance management
 */

#ifndef TYPIO_INSTANCE_H
#define TYPIO_INSTANCE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Instance configuration options
 */
typedef struct TypioInstanceConfig {
    const char *config_dir;     /* Configuration directory path */
    const char *data_dir;       /* Data directory path */
    const char *engine_dir;     /* Engine plugins directory path */
    const char *default_engine; /* Default engine name to activate */
    TypioLogCallback log_callback;  /* Optional log callback */
    void *log_user_data;        /* User data for log callback */
} TypioInstanceConfig;

/**
 * @brief Create a new Typio instance with default configuration
 * @return New instance or nullptr on failure
 */
TypioInstance *typio_instance_new(void);

/**
 * @brief Create a new Typio instance with custom configuration
 * @param config Instance configuration
 * @return New instance or nullptr on failure
 */
TypioInstance *typio_instance_new_with_config(const TypioInstanceConfig *config);

/**
 * @brief Destroy a Typio instance
 * @param instance Instance to destroy
 */
void typio_instance_free(TypioInstance *instance);

/**
 * @brief Initialize the instance (load engines, config, etc.)
 * @param instance Typio instance
 * @return TYPIO_OK on success, error code on failure
 */
TypioResult typio_instance_init(TypioInstance *instance);

/**
 * @brief Shut down the instance
 * @param instance Typio instance
 */
void typio_instance_shutdown(TypioInstance *instance);

/**
 * @brief Get the engine manager
 * @param instance Typio instance
 * @return Engine manager
 */
TypioEngineManager *typio_instance_get_engine_manager(TypioInstance *instance);

/**
 * @brief Create a new input context
 * @param instance Typio instance
 * @return New input context or nullptr on failure
 */
TypioInputContext *typio_instance_create_context(TypioInstance *instance);

/**
 * @brief Destroy an input context
 * @param instance Typio instance
 * @param ctx Input context to destroy
 */
void typio_instance_destroy_context(TypioInstance *instance,
                                     TypioInputContext *ctx);

/**
 * @brief Get the focused input context
 * @param instance Typio instance
 * @return Focused context or nullptr if none
 */
TypioInputContext *typio_instance_get_focused_context(TypioInstance *instance);

/**
 * @brief Set a callback for engine changes
 * @param instance Typio instance
 * @param callback Callback function
 * @param user_data User data passed to callback
 */
void typio_instance_set_engine_changed_callback(TypioInstance *instance,
                                                 TypioEngineChangedCallback callback,
                                                 void *user_data);

/**
 * @brief Set a callback for engine status icon changes
 * @param instance Typio instance
 * @param callback Callback function
 * @param user_data User data passed to callback
 */
void typio_instance_set_status_icon_changed_callback(TypioInstance *instance,
                                                      TypioStatusIconChangedCallback callback,
                                                      void *user_data);

/**
 * @brief Notify that the engine's status icon has changed
 * @param instance Typio instance
 * @param icon_name New icon name
 *
 * Called by engines when their visual status changes (e.g., Rime ascii mode).
 * Only fires the callback if the icon actually changed.
 */
void typio_instance_notify_status_icon(TypioInstance *instance,
                                        const char *icon_name);

/**
 * @brief Get the configuration directory path
 * @param instance Typio instance
 * @return Configuration directory path
 */
const char *typio_instance_get_config_dir(TypioInstance *instance);

/**
 * @brief Get the data directory path
 * @param instance Typio instance
 * @return Data directory path
 */
const char *typio_instance_get_data_dir(TypioInstance *instance);

/**
 * @brief Reload configuration
 * @param instance Typio instance
 * @return TYPIO_OK on success, error code on failure
 */
TypioResult typio_instance_reload_config(TypioInstance *instance);

/**
 * @brief Save current configuration
 * @param instance Typio instance
 * @return TYPIO_OK on success, error code on failure
 */
TypioResult typio_instance_save_config(TypioInstance *instance);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_INSTANCE_H */
