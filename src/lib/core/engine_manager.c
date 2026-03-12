/**
 * @file engine_manager.c
 * @brief Engine manager implementation
 */

#include "typio/engine_manager.h"
#include "typio/instance.h"
#include "typio/engine.h"
#include "../utils/log.h"
#include "../utils/string.h"
#include "../utils/list.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <dlfcn.h>

/* Engine registry entry */
typedef struct EngineEntry {
    char *name;
    char *library_path;
    void *library_handle;
    TypioEngineFactory factory;
    TypioEngineInfoFunc info_func;
    const TypioEngineInfo *info;
    TypioEngine *instance;
    bool is_builtin;
} EngineEntry;

struct TypioEngineManager {
    TypioInstance *instance;
    EngineEntry **entries;
    size_t entry_count;
    size_t entry_capacity;
    size_t active_index;
    const char **name_list;
    size_t name_list_size;
};

static void free_engine_entry(EngineEntry *entry) {
    if (!entry) {
        return;
    }

    if (entry->instance) {
        typio_engine_free(entry->instance);
    }

    if (entry->library_handle) {
        dlclose(entry->library_handle);
    }

    free(entry->name);
    free(entry->library_path);
    free(entry);
}

TypioEngineManager *typio_engine_manager_new(TypioInstance *instance) {
    TypioEngineManager *manager = calloc(1, sizeof(TypioEngineManager));
    if (!manager) {
        return NULL;
    }

    manager->instance = instance;
    manager->entry_capacity = 8;
    manager->entries = calloc(manager->entry_capacity, sizeof(EngineEntry *));
    if (!manager->entries) {
        free(manager);
        return NULL;
    }

    manager->active_index = (size_t)-1;  /* No active engine */

    return manager;
}

static char *get_engine_config_path(TypioEngineManager *manager,
                                    const char *engine_name) {
    const char *config_dir = typio_instance_get_config_dir(manager->instance);
    if (!config_dir || !*config_dir) {
        return NULL;
    }

    size_t len = strlen(config_dir) + strlen(engine_name) + 15;
    char *path = malloc(len);
    if (!path) {
        return NULL;
    }

    snprintf(path, len, "%s/engines/%s.conf", config_dir, engine_name);
    return path;
}

void typio_engine_manager_free(TypioEngineManager *manager) {
    if (!manager) {
        return;
    }

    /* Free all entries */
    for (size_t i = 0; i < manager->entry_count; i++) {
        free_engine_entry(manager->entries[i]);
    }
    free(manager->entries);

    free(manager->name_list);
    free(manager);
}

static EngineEntry *find_entry_by_name(TypioEngineManager *manager,
                                        const char *name) {
    for (size_t i = 0; i < manager->entry_count; i++) {
        if (strcmp(manager->entries[i]->name, name) == 0) {
            return manager->entries[i];
        }
    }
    return NULL;
}

static TypioResult add_entry(TypioEngineManager *manager, EngineEntry *entry) {
    /* Check for duplicate */
    if (find_entry_by_name(manager, entry->name)) {
        typio_log_warning("Engine already registered: %s", entry->name);
        return TYPIO_ERROR_ALREADY_EXISTS;
    }

    /* Grow array if needed */
    if (manager->entry_count >= manager->entry_capacity) {
        size_t new_capacity = manager->entry_capacity * 2;
        EngineEntry **new_entries = realloc(
            manager->entries, new_capacity * sizeof(EngineEntry *));
        if (!new_entries) {
            return TYPIO_ERROR_OUT_OF_MEMORY;
        }
        manager->entries = new_entries;
        manager->entry_capacity = new_capacity;
    }

    manager->entries[manager->entry_count++] = entry;

    /* Invalidate name list cache */
    free(manager->name_list);
    manager->name_list = NULL;
    manager->name_list_size = 0;

    return TYPIO_OK;
}

int typio_engine_manager_load_dir(TypioEngineManager *manager,
                                   const char *path) {
    if (!manager || !path) {
        return -1;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        typio_log_debug("Cannot open engine directory: %s", path);
        return 0;
    }

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        /* Check for .so files */
        const char *name = entry->d_name;
        size_t len = strlen(name);

        if (len > 3 && strcmp(name + len - 3, ".so") == 0) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, name);

            if (typio_engine_manager_load(manager, full_path) == TYPIO_OK) {
                count++;
            }
        }
    }

    closedir(dir);
    return count;
}

TypioResult typio_engine_manager_load(TypioEngineManager *manager,
                                       const char *path) {
    if (!manager || !path) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    typio_log_debug("Loading engine from: %s", path);

    /* Load shared library */
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        typio_log_error("Failed to load engine library: %s (%s)",
                        path, dlerror());
        return TYPIO_ERROR_ENGINE_LOAD_FAILED;
    }

    /* Get info function (use union to avoid void*-to-function-pointer cast) */
    TypioEngineInfoFunc info_func;
    {
        union { void *p; TypioEngineInfoFunc f; } u;
        u.p = dlsym(handle, "typio_engine_get_info");
        info_func = u.f;
    }
    if (!info_func) {
        typio_log_error("Engine library missing typio_engine_get_info: %s", path);
        dlclose(handle);
        return TYPIO_ERROR_ENGINE_LOAD_FAILED;
    }

    /* Get factory function */
    TypioEngineFactory factory;
    {
        union { void *p; TypioEngineFactory f; } u;
        u.p = dlsym(handle, "typio_engine_create");
        factory = u.f;
    }
    if (!factory) {
        typio_log_error("Engine library missing typio_engine_create: %s", path);
        dlclose(handle);
        return TYPIO_ERROR_ENGINE_LOAD_FAILED;
    }

    /* Get engine info */
    const TypioEngineInfo *info = info_func();
    if (!info || !info->name) {
        typio_log_error("Engine returned invalid info: %s", path);
        dlclose(handle);
        return TYPIO_ERROR_ENGINE_LOAD_FAILED;
    }

    /* Check API version */
    if (info->api_version > TYPIO_API_VERSION) {
        typio_log_error("Engine requires newer API version: %s (need %d, have %d)",
                        info->name, info->api_version, TYPIO_API_VERSION);
        dlclose(handle);
        return TYPIO_ERROR_ENGINE_LOAD_FAILED;
    }

    /* Create entry */
    EngineEntry *entry = calloc(1, sizeof(EngineEntry));
    if (!entry) {
        dlclose(handle);
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    entry->name = typio_strdup(info->name);
    entry->library_path = typio_strdup(path);
    entry->library_handle = handle;
    entry->factory = factory;
    entry->info_func = info_func;
    entry->info = info;
    entry->is_builtin = false;

    TypioResult result = add_entry(manager, entry);
    if (result != TYPIO_OK) {
        free_engine_entry(entry);
        return result;
    }

    typio_log_info("Loaded engine: %s (%s)", info->name, info->display_name);

    return TYPIO_OK;
}

TypioResult typio_engine_manager_register(TypioEngineManager *manager,
                                           TypioEngineFactory factory,
                                           TypioEngineInfoFunc info_func) {
    if (!manager || !factory || !info_func) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    const TypioEngineInfo *info = info_func();
    if (!info || !info->name) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    EngineEntry *entry = calloc(1, sizeof(EngineEntry));
    if (!entry) {
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    entry->name = typio_strdup(info->name);
    entry->factory = factory;
    entry->info_func = info_func;
    entry->info = info;
    entry->is_builtin = true;

    TypioResult result = add_entry(manager, entry);
    if (result != TYPIO_OK) {
        free_engine_entry(entry);
        return result;
    }

    typio_log_info("Registered built-in engine: %s", info->name);

    return TYPIO_OK;
}

TypioResult typio_engine_manager_unload(TypioEngineManager *manager,
                                         const char *name) {
    if (!manager || !name) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < manager->entry_count; i++) {
        if (strcmp(manager->entries[i]->name, name) == 0) {
            /* Deactivate if active */
            if (i == manager->active_index) {
                manager->active_index = (size_t)-1;
            } else if (manager->active_index > i) {
                manager->active_index--;
            }

            free_engine_entry(manager->entries[i]);

            /* Shift remaining entries */
            memmove(&manager->entries[i], &manager->entries[i + 1],
                    (manager->entry_count - i - 1) * sizeof(EngineEntry *));
            manager->entry_count--;

            /* Invalidate name list cache */
            free(manager->name_list);
            manager->name_list = NULL;
            manager->name_list_size = 0;

            return TYPIO_OK;
        }
    }

    return TYPIO_ERROR_NOT_FOUND;
}

const char **typio_engine_manager_list(TypioEngineManager *manager,
                                        size_t *count) {
    if (!manager) {
        if (count) *count = 0;
        return NULL;
    }

    /* Build cached list if needed */
    if (!manager->name_list || manager->name_list_size != manager->entry_count) {
        free(manager->name_list);
        manager->name_list = malloc((manager->entry_count + 1) * sizeof(char *));
        if (!manager->name_list) {
            if (count) *count = 0;
            return NULL;
        }

        for (size_t i = 0; i < manager->entry_count; i++) {
            manager->name_list[i] = manager->entries[i]->name;
        }
        manager->name_list[manager->entry_count] = NULL;
        manager->name_list_size = manager->entry_count;
    }

    if (count) *count = manager->entry_count;
    return manager->name_list;
}

const TypioEngineInfo *typio_engine_manager_get_info(TypioEngineManager *manager,
                                                      const char *name) {
    if (!manager || !name) {
        return NULL;
    }

    EngineEntry *entry = find_entry_by_name(manager, name);
    return entry ? entry->info : NULL;
}

TypioEngine *typio_engine_manager_get_engine(TypioEngineManager *manager,
                                              const char *name) {
    if (!manager || !name) {
        return NULL;
    }

    EngineEntry *entry = find_entry_by_name(manager, name);
    if (!entry) {
        return NULL;
    }

    /* Create instance if not exists */
    if (!entry->instance) {
        entry->instance = entry->factory();
        if (!entry->instance) {
            typio_log_error("Failed to create engine instance: %s", name);
            return NULL;
        }
        
        /* Set XDG config path */
        char *config_path = get_engine_config_path(manager, entry->name);
        if (config_path) {
            typio_engine_set_config_path(entry->instance, config_path);
            free(config_path);
        }
    }

    return entry->instance;
}

/* External declaration from engine.c */
extern TypioResult typio_engine_activate(TypioEngine *engine,
                                          TypioInstance *instance);
extern void typio_engine_deactivate(TypioEngine *engine);
extern void typio_instance_notify_engine_changed(TypioInstance *instance,
                                                  const TypioEngineInfo *engine);

TypioResult typio_engine_manager_set_active(TypioEngineManager *manager,
                                             const char *name) {
    if (!manager || !name) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    /* Find entry */
    size_t index = (size_t)-1;
    for (size_t i = 0; i < manager->entry_count; i++) {
        if (strcmp(manager->entries[i]->name, name) == 0) {
            index = i;
            break;
        }
    }

    if (index == (size_t)-1) {
        return TYPIO_ERROR_NOT_FOUND;
    }

    /* Already active? */
    if (index == manager->active_index) {
        return TYPIO_OK;
    }

    /* Deactivate current engine */
    if (manager->active_index != (size_t)-1) {
        EngineEntry *current = manager->entries[manager->active_index];
        if (current->instance) {
            typio_engine_deactivate(current->instance);
        }
    }

    /* Get or create engine instance */
    EngineEntry *entry = manager->entries[index];
    if (!entry->instance) {
        entry->instance = entry->factory();
        if (!entry->instance) {
            typio_log_error("Failed to create engine instance: %s", name);
            return TYPIO_ERROR_ENGINE_LOAD_FAILED;
        }

        /* Set XDG config path */
        char *config_path = get_engine_config_path(manager, entry->name);
        if (config_path) {
            typio_engine_set_config_path(entry->instance, config_path);
            free(config_path);
        }
    }

    /* Activate new engine */
    TypioResult result = typio_engine_activate(entry->instance, manager->instance);
    if (result != TYPIO_OK) {
        return result;
    }

    manager->active_index = index;

    /* Notify instance */
    typio_instance_notify_engine_changed(manager->instance, entry->info);

    return TYPIO_OK;
}

TypioEngine *typio_engine_manager_get_active(TypioEngineManager *manager) {
    if (!manager || manager->active_index == (size_t)-1) {
        return NULL;
    }

    return manager->entries[manager->active_index]->instance;
}

TypioResult typio_engine_manager_next(TypioEngineManager *manager) {
    if (!manager || manager->entry_count == 0) {
        return TYPIO_ERROR_ENGINE_NOT_AVAILABLE;
    }

    size_t next_index;
    if (manager->active_index == (size_t)-1) {
        next_index = 0;
    } else {
        next_index = (manager->active_index + 1) % manager->entry_count;
    }

    return typio_engine_manager_set_active(manager,
                                            manager->entries[next_index]->name);
}

TypioResult typio_engine_manager_prev(TypioEngineManager *manager) {
    if (!manager || manager->entry_count == 0) {
        return TYPIO_ERROR_ENGINE_NOT_AVAILABLE;
    }

    size_t prev_index;
    if (manager->active_index == (size_t)-1) {
        prev_index = manager->entry_count - 1;
    } else if (manager->active_index == 0) {
        prev_index = manager->entry_count - 1;
    } else {
        prev_index = manager->active_index - 1;
    }

    return typio_engine_manager_set_active(manager,
                                            manager->entries[prev_index]->name);
}
