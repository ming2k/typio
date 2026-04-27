/**
 * @file engine_manager.c
 * @brief Engine manager implementation
 */

#include "typio/engine_manager.h"
#include "typio/instance.h"
#include "typio/engine.h"
#include "typio/config.h"
#include "../utils/log.h"
#include "../utils/string.h"
#include "../utils/list.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <time.h>

#define TYPIO_ENGINE_CONFIG_SUFFIX ".toml"
#define TYPIO_SWITCH_STABLE_THRESHOLD_MS_DEFAULT 1000

static uint64_t engine_manager_switch_threshold_ms(void)
{
	const char *env = getenv("TYPIO_SWITCH_STABLE_THRESHOLD_MS");
	if (env) {
		unsigned long v = strtoul(env, NULL, 10);
		if (v > 0) return (uint64_t)v;
	}
	return TYPIO_SWITCH_STABLE_THRESHOLD_MS_DEFAULT;
}
#define TYPIO_ENGINE_STATE_FILE "engine-state.toml"
#define TYPIO_ENGINE_STATE_PRIMARY_KEY "recent.primary"
#define TYPIO_ENGINE_STATE_SECONDARY_KEY "recent.secondary"

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

static EngineEntry *find_entry_by_name(TypioEngineManager *manager,
                                       const char *name);

static uint64_t engine_manager_monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

struct TypioEngineManager {
    TypioInstance *instance;
    EngineEntry **entries;
    size_t entry_count;
    size_t entry_capacity;
    size_t active_keyboard_index;
    size_t active_voice_index;
    uint64_t last_switch_ms;
    char *recent_primary_name;
    char *recent_secondary_name;
    const char **name_list;
    size_t name_list_size;
    const char **ordered_keyboard_list;
    size_t ordered_keyboard_list_size;
};

static char *engine_manager_build_state_path(TypioEngineManager *manager) {
    const char *state_dir;
    size_t len;
    char *path;

    if (!manager || !manager->instance) {
        return nullptr;
    }

    state_dir = typio_instance_get_state_dir(manager->instance);
    if (!state_dir || !*state_dir) {
        return nullptr;
    }

    len = strlen(state_dir) + strlen(TYPIO_ENGINE_STATE_FILE) + 2;
    path = malloc(len);
    if (!path) {
        return nullptr;
    }

    snprintf(path, len, "%s/%s", state_dir, TYPIO_ENGINE_STATE_FILE);
    return path;
}

static bool engine_manager_strings_equal(const char *a, const char *b) {
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    return strcmp(a, b) == 0;
}

static bool engine_manager_name_is_keyboard(TypioEngineManager *manager,
                                            const char *name) {
    EngineEntry *entry;

    if (!manager || !name || !*name) {
        return false;
    }

    entry = find_entry_by_name(manager, name);
    return entry && entry->info &&
           entry->info->type == TYPIO_ENGINE_TYPE_KEYBOARD;
}

static void engine_manager_load_recent_state(TypioEngineManager *manager) {
    TypioConfig *state = NULL;
    char *path;
    const char *primary;
    const char *secondary;

    if (!manager) {
        return;
    }

    path = engine_manager_build_state_path(manager);
    if (!path) {
        return;
    }

    state = typio_config_load_file(path);
    free(path);
    if (!state) {
        return;
    }

    primary = typio_config_get_string(state, TYPIO_ENGINE_STATE_PRIMARY_KEY, NULL);
    secondary = typio_config_get_string(state, TYPIO_ENGINE_STATE_SECONDARY_KEY, NULL);

    free(manager->recent_primary_name);
    free(manager->recent_secondary_name);
    manager->recent_primary_name = primary && *primary ? strdup(primary) : NULL;
    manager->recent_secondary_name = secondary && *secondary ? strdup(secondary) : NULL;

    typio_config_free(state);
}

static void engine_manager_save_recent_state(TypioEngineManager *manager) {
    TypioConfig *state;
    char *path;

    if (!manager) {
        return;
    }

    path = engine_manager_build_state_path(manager);
    if (!path) {
        return;
    }

    state = typio_config_new();
    if (!state) {
        free(path);
        return;
    }

    if (manager->recent_primary_name && *manager->recent_primary_name) {
        typio_config_set_string(state,
                                TYPIO_ENGINE_STATE_PRIMARY_KEY,
                                manager->recent_primary_name);
    }
    if (manager->recent_secondary_name && *manager->recent_secondary_name) {
        typio_config_set_string(state,
                                TYPIO_ENGINE_STATE_SECONDARY_KEY,
                                manager->recent_secondary_name);
    }

    typio_config_save_file(state, path);
    typio_config_free(state);
    free(path);
}

static void engine_manager_update_recent_pair(TypioEngineManager *manager,
                                              const char *stable_name) {
    char *new_primary = NULL;
    char *new_secondary = NULL;
    bool changed = false;

    if (!manager || !stable_name || !*stable_name) {
        return;
    }

    if (manager->recent_primary_name &&
        strcmp(manager->recent_primary_name, stable_name) == 0) {
        return;
    }

    new_primary = strdup(stable_name);
    if (!new_primary) {
        return;
    }

    if (manager->recent_primary_name &&
        strcmp(manager->recent_primary_name, stable_name) != 0) {
        new_secondary = strdup(manager->recent_primary_name);
    } else if (manager->recent_secondary_name &&
               strcmp(manager->recent_secondary_name, stable_name) != 0) {
        new_secondary = strdup(manager->recent_secondary_name);
    }

    changed = !engine_manager_strings_equal(manager->recent_primary_name, new_primary) ||
              !engine_manager_strings_equal(manager->recent_secondary_name, new_secondary);

    free(manager->recent_primary_name);
    free(manager->recent_secondary_name);
    manager->recent_primary_name = new_primary;
    manager->recent_secondary_name = new_secondary;

    if (changed) {
        engine_manager_save_recent_state(manager);
    }
}

static size_t engine_manager_recent_partner_index(TypioEngineManager *manager) {
    const char *current_name;
    const char *partner_name = NULL;

    if (!manager || manager->active_keyboard_index == (size_t)-1) {
        return (size_t)-1;
    }

    current_name = manager->entries[manager->active_keyboard_index]->name;
    if (manager->recent_primary_name &&
        strcmp(manager->recent_primary_name, current_name) == 0) {
        partner_name = manager->recent_secondary_name;
    } else if (manager->recent_secondary_name &&
               strcmp(manager->recent_secondary_name, current_name) == 0) {
        partner_name = manager->recent_primary_name;
    }

    if (!engine_manager_name_is_keyboard(manager, partner_name)) {
        return (size_t)-1;
    }

    for (size_t i = 0; i < manager->entry_count; i++) {
        if (strcmp(manager->entries[i]->name, partner_name) == 0) {
            return i;
        }
    }

    return (size_t)-1;
}

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
        return nullptr;
    }

    manager->instance = instance;
    manager->entry_capacity = 8;
    manager->entries = calloc(manager->entry_capacity, sizeof(EngineEntry *));
    if (!manager->entries) {
        free(manager);
        return nullptr;
    }

    manager->active_keyboard_index = (size_t)-1;
    manager->active_voice_index = (size_t)-1;
    engine_manager_load_recent_state(manager);

    return manager;
}

static char *get_engine_config_path(TypioEngineManager *manager,
                                    const char *engine_name) {
    const char *config_dir = typio_instance_get_config_dir(manager->instance);
    if (!config_dir || !*config_dir) {
        return nullptr;
    }

    size_t len = strlen(config_dir) + strlen(engine_name) + 16;
    char *path = malloc(len);
    if (!path) {
        return nullptr;
    }

    snprintf(path, len, "%s/engines/%s%s",
             config_dir, engine_name, TYPIO_ENGINE_CONFIG_SUFFIX);
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
    
    /* Just free the array, entries owned by manager */
    if (manager->ordered_keyboard_list) {
        free(manager->ordered_keyboard_list);
    }
    free(manager->recent_primary_name);
    free(manager->recent_secondary_name);
    free(manager);
}

static EngineEntry *find_entry_by_name(TypioEngineManager *manager,
                                        const char *name) {
    for (size_t i = 0; i < manager->entry_count; i++) {
        if (strcmp(manager->entries[i]->name, name) == 0) {
            return manager->entries[i];
        }
    }
    return nullptr;
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
    manager->name_list = nullptr;
    manager->name_list_size = 0;
    free(manager->ordered_keyboard_list);
    manager->ordered_keyboard_list = nullptr;
    manager->ordered_keyboard_list_size = 0;

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

    while ((entry = readdir(dir)) != nullptr) {
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
            /* Deactivate if active (keyboard slot) */
            if (i == manager->active_keyboard_index) {
                manager->active_keyboard_index = (size_t)-1;
            } else if (manager->active_keyboard_index != (size_t)-1 &&
                       manager->active_keyboard_index > i) {
                manager->active_keyboard_index--;
            }

            /* Deactivate if active (voice slot) */
            if (i == manager->active_voice_index) {
                manager->active_voice_index = (size_t)-1;
            } else if (manager->active_voice_index != (size_t)-1 &&
                       manager->active_voice_index > i) {
                manager->active_voice_index--;
            }

            free_engine_entry(manager->entries[i]);

            /* Shift remaining entries */
            memmove(&manager->entries[i], &manager->entries[i + 1],
                    (manager->entry_count - i - 1) * sizeof(EngineEntry *));
            manager->entry_count--;

            /* Invalidate name list cache */
            free(manager->name_list);
            manager->name_list = nullptr;
            manager->name_list_size = 0;
            free(manager->ordered_keyboard_list);
            manager->ordered_keyboard_list = nullptr;
            manager->ordered_keyboard_list_size = 0;

            return TYPIO_OK;
        }
    }

    return TYPIO_ERROR_NOT_FOUND;
}

const char **typio_engine_manager_list(TypioEngineManager *manager,
                                        size_t *count) {
    if (!manager) {
        if (count) *count = 0;
        return nullptr;
    }

    /* Build cached list if needed */
    if (!manager->name_list || manager->name_list_size != manager->entry_count) {
        free(manager->name_list);
        manager->name_list = malloc((manager->entry_count + 1) * sizeof(char *));
        if (!manager->name_list) {
            if (count) *count = 0;
            return nullptr;
        }

        for (size_t i = 0; i < manager->entry_count; i++) {
            manager->name_list[i] = manager->entries[i]->name;
        }
        manager->name_list[manager->entry_count] = nullptr;
        manager->name_list_size = manager->entry_count;
    }

    if (count) *count = manager->entry_count;
    return manager->name_list;
}

const char **typio_engine_manager_list_by_type(TypioEngineManager *manager,
                                               TypioEngineType type,
                                               size_t *count) {
    const char **all_names;
    const TypioEngineInfo *info;
    const char **filtered;
    size_t all_count = 0;
    size_t filtered_count = 0;

    if (!manager) {
        if (count) {
            *count = 0;
        }
        return nullptr;
    }

    all_names = typio_engine_manager_list(manager, &all_count);
    if (!all_names) {
        if (count) {
            *count = 0;
        }
        return nullptr;
    }

    filtered = calloc(all_count + 1, sizeof(char *));
    if (!filtered) {
        if (count) {
            *count = 0;
        }
        return nullptr;
    }

    for (size_t i = 0; i < all_count; ++i) {
        info = typio_engine_manager_get_info(manager, all_names[i]);
        if (!info || info->type != type) {
            continue;
        }
        filtered[filtered_count++] = all_names[i];
    }

    filtered[filtered_count] = nullptr;
    if (count) {
        *count = filtered_count;
    }
    return filtered;
}

void engine_manager_reset_ordered_list(TypioEngineManager *manager) {
    if (manager->ordered_keyboard_list) {
        free(manager->ordered_keyboard_list);
        manager->ordered_keyboard_list = NULL;
    }
    manager->ordered_keyboard_list_size = 0;
}

const char **typio_engine_manager_list_ordered_keyboards(TypioEngineManager *manager,
                                                          size_t *count) {
    TypioConfig *config;
    size_t keyboard_count = 0;
    size_t write_index = 0;

    if (!manager) {
        if (count) {
            *count = 0;
        }
        return nullptr;
    }

    engine_manager_reset_ordered_list(manager);

    for (size_t i = 0; i < manager->entry_count; ++i) {
        if (manager->entries[i]->info->type == TYPIO_ENGINE_TYPE_KEYBOARD) {
            keyboard_count++;
        }
    }

    manager->ordered_keyboard_list = calloc(keyboard_count + 1, sizeof(char *));
    if (!manager->ordered_keyboard_list) {
        if (count) {
            *count = 0;
        }
        return nullptr;
    }

    config = manager->instance ? typio_instance_get_config(manager->instance) : NULL;
    if (config) {
        size_t order_count = typio_config_get_array_size(config, "engine_order");
        for (size_t i = 0; i < order_count; ++i) {
            const char *name = typio_config_get_array_string(config, "engine_order", i);
            if (!engine_manager_name_is_keyboard(manager, name)) {
                continue;
            }
            if (find_entry_by_name(manager, name) == NULL) {
                continue;
            }
            bool seen = false;
            for (size_t j = 0; j < write_index; ++j) {
                if (strcmp(manager->ordered_keyboard_list[j], name) == 0) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                manager->ordered_keyboard_list[write_index++] = name;
            }
        }
    }

    for (size_t i = 0; i < manager->entry_count; ++i) {
        const char *name = manager->entries[i]->name;
        bool seen = false;

        if (manager->entries[i]->info->type != TYPIO_ENGINE_TYPE_KEYBOARD) {
            continue;
        }

        for (size_t j = 0; j < write_index; ++j) {
            if (strcmp(manager->ordered_keyboard_list[j], name) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            manager->ordered_keyboard_list[write_index++] = name;
        }
    }

    manager->ordered_keyboard_list[write_index] = NULL;
    manager->ordered_keyboard_list_size = write_index;
    if (count) {
        *count = write_index;
    }
    return manager->ordered_keyboard_list;
}

const TypioEngineInfo *typio_engine_manager_get_info(TypioEngineManager *manager,
                                                      const char *name) {
    if (!manager || !name) {
        return nullptr;
    }

    EngineEntry *entry = find_entry_by_name(manager, name);
    return entry ? entry->info : nullptr;
}

TypioEngine *typio_engine_manager_get_engine(TypioEngineManager *manager,
                                              const char *name) {
    if (!manager || !name) {
        return nullptr;
    }

    EngineEntry *entry = find_entry_by_name(manager, name);
    if (!entry) {
        return nullptr;
    }

    /* Create instance if not exists */
    if (!entry->instance) {
        entry->instance = entry->factory();
        if (!entry->instance) {
            typio_log_error("Failed to create engine instance: %s", name);
            return nullptr;
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

static void engine_manager_rebind_focused_context(TypioEngineManager *manager,
                                                  TypioEngine *old_engine,
                                                  TypioEngine *new_engine) {
    TypioInputContext *ctx;

    if (!manager || !manager->instance) {
        return;
    }

    ctx = typio_instance_get_focused_context(manager->instance);
    if (!ctx) {
        return;
    }

    if (old_engine && old_engine->ops && old_engine->ops->focus_out) {
        old_engine->ops->focus_out(old_engine, ctx);
    }
    /* The old engine's focus_out may have emitted a status icon/mode update
     * (e.g. Rime refreshes its icon during reset).  Clear the stale
     * value so the new engine starts with a clean slate. */
    typio_instance_clear_status_icon(manager->instance);
    typio_instance_clear_mode(manager->instance);
    if (new_engine && new_engine->ops && new_engine->ops->reset) {
        new_engine->ops->reset(new_engine, ctx);
    }
    if (new_engine && new_engine->ops && new_engine->ops->focus_in) {
        new_engine->ops->focus_in(new_engine, ctx);
    }
    /* Prefer structured get_mode; fall back to legacy get_status_icon. */
    if (new_engine && new_engine->ops && new_engine->ops->get_mode) {
        const TypioEngineMode *mode = new_engine->ops->get_mode(new_engine, ctx);
        if (mode) {
            typio_instance_notify_mode(manager->instance, mode);
        }
    } else if (new_engine && new_engine->ops && new_engine->ops->get_status_icon) {
        const char *icon = new_engine->ops->get_status_icon(new_engine, ctx);
        if (icon) {
            typio_instance_notify_status_icon(manager->instance, icon);
        }
    }
}

static TypioResult engine_manager_ensure_entry_instance(TypioEngineManager *manager,
                                                        EngineEntry *entry) {
    char *config_path;

    if (!manager || !entry) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    if (entry->instance) {
        return TYPIO_OK;
    }

    entry->instance = entry->factory();
    if (!entry->instance) {
        typio_log_error("Failed to create engine instance: %s", entry->name);
        return TYPIO_ERROR_ENGINE_LOAD_FAILED;
    }

    config_path = get_engine_config_path(manager, entry->name);
    if (config_path) {
        typio_engine_set_config_path(entry->instance, config_path);
        free(config_path);
    }

    return TYPIO_OK;
}

static void engine_manager_try_restore_engine(EngineEntry *entry,
                                              TypioInstance *instance,
                                              const char *slot_name) {
    TypioResult restore_result;

    if (!entry || !entry->instance) {
        return;
    }

    restore_result = typio_engine_activate(entry->instance, instance);
    if (restore_result != TYPIO_OK) {
        typio_log_error("Failed to restore previous %s engine '%s' after switch failure: %d",
                        slot_name ? slot_name : "active",
                        entry->name ? entry->name : "(unknown)",
                        restore_result);
    }
}

TypioResult typio_engine_manager_set_active(TypioEngineManager *manager,
                                             const char *name) {
    uint64_t now_ms;

    if (!manager || !name) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    now_ms = engine_manager_monotonic_ms();

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

    EngineEntry *entry = manager->entries[index];
    bool is_voice = entry->info->type == TYPIO_ENGINE_TYPE_VOICE;

    /* Route to voice slot if this is a voice engine */
    if (is_voice) {
        EngineEntry *current = nullptr;

        if (index == manager->active_voice_index) {
            return TYPIO_OK;
        }

        /* Deactivate current voice engine */
        if (manager->active_voice_index != (size_t)-1) {
            current = manager->entries[manager->active_voice_index];
            if (current->instance) {
                typio_engine_deactivate(current->instance);
            }
        }

        TypioResult result = engine_manager_ensure_entry_instance(manager, entry);
        if (result != TYPIO_OK) {
            engine_manager_try_restore_engine(current, manager->instance, "voice");
            return result;
        }

        result = typio_engine_activate(entry->instance, manager->instance);
        if (result != TYPIO_OK) {
            engine_manager_try_restore_engine(current, manager->instance, "voice");
            return result;
        }

        manager->active_voice_index = index;
        typio_instance_notify_voice_engine_changed(manager->instance, entry->info);
        typio_log_info("Active voice engine: %s", entry->info->name);
        return TYPIO_OK;
    }

    /* Keyboard engine path */
    if (index == manager->active_keyboard_index) {
        return TYPIO_OK;
    }

    /* Deactivate current keyboard engine */
    EngineEntry *current = nullptr;
    if (manager->active_keyboard_index != (size_t)-1) {
        current = manager->entries[manager->active_keyboard_index];
        if (current->instance) {
            typio_engine_deactivate(current->instance);
        }
    }

    TypioResult result = engine_manager_ensure_entry_instance(manager, entry);
    if (result != TYPIO_OK) {
        engine_manager_try_restore_engine(current, manager->instance, "keyboard");
        return result;
    }

    /* Activate new engine */
    result = typio_engine_activate(entry->instance, manager->instance);
    if (result != TYPIO_OK) {
        engine_manager_try_restore_engine(current, manager->instance, "keyboard");
        return result;
    }

    manager->active_keyboard_index = index;
    manager->last_switch_ms = now_ms;

    /* Clear stale status icon/mode from the previous engine.  This covers
     * the no-focused-context path where rebind returns early.  The clear
     * inside rebind_focused_context handles the case where focus_out
     * re-sets the icon before the new engine takes over. */
    typio_instance_clear_status_icon(manager->instance);
    typio_instance_clear_mode(manager->instance);

    engine_manager_rebind_focused_context(manager,
                                          current ? current->instance : nullptr,
                                          entry->instance);

    /* Publish the engine change only after focused-context rebind has
     * finished so tray/state observers see a consistent engine+icon+session. */
    typio_instance_notify_engine_changed(manager->instance, entry->info);

    return TYPIO_OK;
}

TypioEngine *typio_engine_manager_get_active(TypioEngineManager *manager) {
    if (!manager || manager->active_keyboard_index == (size_t)-1) {
        return nullptr;
    }

    return manager->entries[manager->active_keyboard_index]->instance;
}

TypioEngine *typio_engine_manager_get_active_by_type(TypioEngineManager *manager,
                                                     TypioEngineType type) {
    if (type == TYPIO_ENGINE_TYPE_VOICE) {
        return typio_engine_manager_get_active_voice(manager);
    }

    return typio_engine_manager_get_active(manager);
}

TypioResult typio_engine_manager_set_active_voice(TypioEngineManager *manager,
                                                   const char *name) {
    if (!manager || !name) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    /* Validate that the named engine is actually a voice engine */
    const TypioEngineInfo *info = typio_engine_manager_get_info(manager, name);
    if (!info) {
        return TYPIO_ERROR_NOT_FOUND;
    }
    if (info->type != TYPIO_ENGINE_TYPE_VOICE) {
        typio_log_error("Engine '%s' is not a voice engine", name);
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    return typio_engine_manager_set_active(manager, name);
}

TypioEngine *typio_engine_manager_get_active_voice(TypioEngineManager *manager) {
    if (!manager || manager->active_voice_index == (size_t)-1) {
        return nullptr;
    }

    return manager->entries[manager->active_voice_index]->instance;
}

/**
 * Resolve the target engine for a next/prev switch.
 *
 * Slow switch (elapsed > threshold): toggle between the two most recently
 * committed engines.  Fast switch (elapsed <= threshold): cycle through
 * the ordered keyboard engine list.
 *
 * @param direction  +1 for next, -1 for prev
 */
static const char *engine_manager_resolve_switch(TypioEngineManager *manager,
                                                 const char **ordered,
                                                 size_t ordered_count,
                                                 int direction) {

    if (manager->active_keyboard_index == (size_t)-1) {
        return ordered_count > 0
            ? ordered[direction > 0 ? 0 : ordered_count - 1]
            : NULL;
    }

    uint64_t now_ms = engine_manager_monotonic_ms();
    uint64_t elapsed = now_ms - manager->last_switch_ms;

    /* Slow switch: toggle between recent committed pair */
    if (elapsed > engine_manager_switch_threshold_ms()) {
        size_t partner = engine_manager_recent_partner_index(manager);
        if (partner != (size_t)-1) {
            return manager->entries[partner]->name;
        }
    }

    /* Fast switch (or no partner available): cycle ordered list */
    const char *active_name = manager->entries[manager->active_keyboard_index]->name;
    size_t current_ordered = (size_t)-1;
    for (size_t i = 0; i < ordered_count; ++i) {
        if (strcmp(ordered[i], active_name) == 0) {
            current_ordered = i;
            break;
        }
    }

    if (current_ordered == (size_t)-1) {
        return ordered_count > 0
            ? ordered[direction > 0 ? 0 : ordered_count - 1]
            : NULL;
    }

    if (direction > 0) {
        return ordered[(current_ordered + 1) % ordered_count];
    }
    return ordered[current_ordered == 0 ? ordered_count - 1 : current_ordered - 1];
}

TypioResult typio_engine_manager_next(TypioEngineManager *manager) {
    if (!manager || manager->entry_count == 0) {
        return TYPIO_ERROR_ENGINE_NOT_AVAILABLE;
    }

    size_t ordered_count = 0;
    const char **ordered = typio_engine_manager_list_ordered_keyboards(manager, &ordered_count);
    const char *target = engine_manager_resolve_switch(manager, ordered, ordered_count, +1);

    if (!target) {
        return TYPIO_ERROR_ENGINE_NOT_AVAILABLE;
    }
    return typio_engine_manager_set_active(manager, target);
}

TypioResult typio_engine_manager_prev(TypioEngineManager *manager) {
    if (!manager || manager->entry_count == 0) {
        return TYPIO_ERROR_ENGINE_NOT_AVAILABLE;
    }

    size_t ordered_count = 0;
    const char **ordered = typio_engine_manager_list_ordered_keyboards(manager, &ordered_count);
    const char *target = engine_manager_resolve_switch(manager, ordered, ordered_count, -1);

    if (!target) {
        return TYPIO_ERROR_ENGINE_NOT_AVAILABLE;
    }
    return typio_engine_manager_set_active(manager, target);
}

void typio_engine_manager_notify_commit(TypioEngineManager *manager) {
    if (!manager || manager->active_keyboard_index == (size_t)-1) {
        return;
    }

    const char *name = manager->entries[manager->active_keyboard_index]->name;
    engine_manager_update_recent_pair(manager, name);
}
