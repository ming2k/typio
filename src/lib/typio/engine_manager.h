/**
 * @file engine_manager.h
 * @brief Engine manager for loading and managing input engines
 */

#ifndef TYPIO_ENGINE_MANAGER_H
#define TYPIO_ENGINE_MANAGER_H

#include "types.h"
#include "engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new engine manager
 * @param instance Parent Typio instance
 * @return New engine manager or nullptr on failure
 */
TypioEngineManager *typio_engine_manager_new(TypioInstance *instance);

/**
 * @brief Destroy engine manager and unload all engines
 * @param manager Engine manager to destroy
 */
void typio_engine_manager_free(TypioEngineManager *manager);

/**
 * @brief Load engines from a directory
 * @param manager Engine manager
 * @param path Directory path containing engine libraries
 * @return Number of engines loaded, or negative error code
 */
int typio_engine_manager_load_dir(TypioEngineManager *manager, const char *path);

/**
 * @brief Load a specific engine library
 * @param manager Engine manager
 * @param path Path to engine library file
 * @return TYPIO_OK on success, error code on failure
 */
TypioResult typio_engine_manager_load(TypioEngineManager *manager,
                                       const char *path);

/**
 * @brief Register a built-in (statically linked) engine
 * @param manager Engine manager
 * @param factory Engine factory function
 * @param info_func Engine info function
 * @return TYPIO_OK on success, error code on failure
 */
TypioResult typio_engine_manager_register(TypioEngineManager *manager,
                                           TypioEngineFactory factory,
                                           TypioEngineInfoFunc info_func);

/**
 * @brief Unload an engine by name
 * @param manager Engine manager
 * @param name Engine name to unload
 * @return TYPIO_OK on success, error code on failure
 */
TypioResult typio_engine_manager_unload(TypioEngineManager *manager,
                                         const char *name);

/**
 * @brief Get list of available engine names
 * @param manager Engine manager
 * @param count Output parameter for number of engines
 * @return Array of engine names (do not free)
 */
const char **typio_engine_manager_list(TypioEngineManager *manager,
                                        size_t *count);

/**
 * @brief Get engine info by name
 * @param manager Engine manager
 * @param name Engine name
 * @return Engine info or nullptr if not found
 */
const TypioEngineInfo *typio_engine_manager_get_info(TypioEngineManager *manager,
                                                      const char *name);

/**
 * @brief Get or create an engine instance by name
 * @param manager Engine manager
 * @param name Engine name
 * @return Engine instance or nullptr if not found/failed
 */
TypioEngine *typio_engine_manager_get_engine(TypioEngineManager *manager,
                                              const char *name);

/**
 * @brief Set the active engine
 * @param manager Engine manager
 * @param name Engine name to activate
 * @return TYPIO_OK on success, error code on failure
 */
TypioResult typio_engine_manager_set_active(TypioEngineManager *manager,
                                             const char *name);

/**
 * @brief Get the currently active engine
 * @param manager Engine manager
 * @return Active engine or nullptr if none
 */
TypioEngine *typio_engine_manager_get_active(TypioEngineManager *manager);

/**
 * @brief Switch to the next available engine
 * @param manager Engine manager
 * @return TYPIO_OK on success, error code on failure
 */
TypioResult typio_engine_manager_next(TypioEngineManager *manager);

/**
 * @brief Switch to the previous available engine
 * @param manager Engine manager
 * @return TYPIO_OK on success, error code on failure
 */
TypioResult typio_engine_manager_prev(TypioEngineManager *manager);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_ENGINE_MANAGER_H */
