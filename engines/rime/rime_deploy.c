/**
 * @file rime_deploy.c
 * @brief librime deployment and maintenance management
 *
 * In librime 1.0+ all API functions are guaranteed to exist once setup() has
 * been called.  We therefore do not NULL-check individual function pointers.
 */

#include "rime_internal.h"

void typio_rime_invalidate_generated_yaml(TypioRimeState *state) {
    char *build_dir;
    DIR *dir;
    struct dirent *entry;

    if (!state || !state->config.user_data_dir) {
        return;
    }

    build_dir = typio_path_join(state->config.user_data_dir, "build");
    if (!build_dir) {
        return;
    }

    dir = opendir(build_dir);
    if (!dir) {
        free(build_dir);
        return;
    }

    while ((entry = readdir(dir)) != nullptr) {
        char *path;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (!typio_rime_has_yaml_suffix(entry->d_name)) {
            continue;
        }

        path = typio_path_join(build_dir, entry->d_name);
        if (!path) {
            continue;
        }

        if (unlink(path) != 0 && errno != ENOENT) {
            typio_log_warning("Failed to invalidate generated Rime YAML: %s", path);
        }
        free(path);
    }

    closedir(dir);
    free(build_dir);
}

bool typio_rime_run_maintenance(TypioRimeState *state, bool full_check) {
    const char *sync_env = getenv("TYPIO_RIME_SYNC_DEPLOY");
    bool sync = sync_env && strcmp(sync_env, "1") == 0;

    typio_log_info("Rime deployment started (%s)", sync ? "blocking" : "non-blocking");

    state->deploy_id++;

    if (!state->api->start_maintenance(full_check ? True : False)) {
        typio_log_error("Rime deployment failed to start");
        return false;
    }

    if (sync) {
        state->api->join_maintenance_thread();
        state->maintenance_done = true;
    } else {
        state->maintenance_done = false;
    }

    return true;
}

bool typio_rime_is_maintaining(TypioRimeState *state) {
    if (!state) {
        return false;
    }
    return state->api->is_maintenance_mode() ? true : false;
}

bool typio_rime_ensure_deployed(TypioRimeState *state) {
    char *build_path;
    bool need_maintenance;

    if (!state) {
        return false;
    }

    if (state->maintenance_done) {
        return true;
    }

    /* Check if maintenance is currently running */
    if (typio_rime_is_maintaining(state)) {
        return false;
    }

    build_path = typio_path_join(state->config.user_data_dir, "build/default.yaml");
    need_maintenance = !typio_rime_path_exists(build_path);
    free(build_path);

    if (need_maintenance) {
        if (typio_rime_run_maintenance(state, true)) {
            /* If synchronous, it's already done */
            return state->maintenance_done;
        }
        return false;
    }

    /* No maintenance needed or it's already finished */
    state->maintenance_done = true;
    return true;
}
