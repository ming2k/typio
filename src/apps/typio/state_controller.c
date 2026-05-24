/**
 * @file state_controller.c
 * @brief StateController implementation
 */

#include "state_controller.h"
#include "typio/engine_manager.h"
#include "typio/engine.h"
#include "typio/typio.h"
#include "typio/log.h"

#include <stdlib.h>
#include <string.h>

struct TypioStateController {
    TypioInstance *instance;

    /* -- cached state snapshots ------------------------------------------- */
    char *active_engine_name;
    char *active_engine_display_name;
    char *active_voice_engine_name;
    char *active_voice_engine_display_name;
    char *status_icon;

    bool engine_active;

    bool has_mode;
    TypioEngineMode mode;
    char *mode_mode_id;
    char *mode_display_label;
    char *mode_icon_name;

    /* -- listeners -------------------------------------------------------- */
    TypioStateListener *listeners;
    size_t listener_count;
    size_t listener_capacity;
};

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static char *typio_state_strdup(const char *src) {
    if (!src || !*src) {
        return nullptr;
    }
    return strdup(src);
}

static void typio_state_controller_broadcast(TypioStateController *ctrl,
                                             TypioStateChangeType change_type) {
    if (!ctrl) {
        return;
    }
    for (size_t i = 0; i < ctrl->listener_count; i++) {
        TypioStateListener *l = &ctrl->listeners[i];
        if (l->callback) {
            l->callback(l->user_data, change_type);
        }
    }
}

static void typio_state_controller_update_engine_active(
    TypioStateController *ctrl) {
    if (!ctrl || !ctrl->instance) {
        return;
    }
    TypioEngineManager *manager =
        typio_instance_get_engine_manager(ctrl->instance);
    TypioEngine *active =
        manager ? typio_engine_manager_get_active(manager) : nullptr;
    ctrl->engine_active = active != nullptr;
}

static void typio_state_controller_clear_mode(TypioStateController *ctrl) {
    free(ctrl->mode_mode_id);
    free(ctrl->mode_display_label);
    free(ctrl->mode_icon_name);
    ctrl->mode_mode_id = nullptr;
    ctrl->mode_display_label = nullptr;
    ctrl->mode_icon_name = nullptr;
    ctrl->has_mode = false;
    memset(&ctrl->mode, 0, sizeof(ctrl->mode));
}

static void typio_state_controller_set_mode(TypioStateController *ctrl,
                                            const TypioEngineMode *mode) {
    typio_state_controller_clear_mode(ctrl);
    if (!mode) {
        return;
    }
    ctrl->has_mode = true;
    ctrl->mode.mode_class = mode->mode_class;
    ctrl->mode.mode_id = ctrl->mode_mode_id = typio_state_strdup(mode->mode_id);
    ctrl->mode.display_label =
        ctrl->mode_display_label = typio_state_strdup(mode->display_label);
    ctrl->mode.icon_name = ctrl->mode_icon_name = typio_state_strdup(mode->icon_name);
}

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

TypioStateController *typio_state_controller_new(TypioInstance *instance) {
    if (!instance) {
        return nullptr;
    }
    TypioStateController *ctrl = calloc(1, sizeof(TypioStateController));
    if (!ctrl) {
        return nullptr;
    }
    ctrl->instance = instance;
    ctrl->listener_capacity = 4;
    ctrl->listeners = calloc(ctrl->listener_capacity, sizeof(TypioStateListener));
    if (!ctrl->listeners) {
        free(ctrl);
        return nullptr;
    }
    return ctrl;
}

void typio_state_controller_free(TypioStateController *ctrl) {
    if (!ctrl) {
        return;
    }
    free(ctrl->active_engine_name);
    free(ctrl->active_engine_display_name);
    free(ctrl->active_voice_engine_name);
    free(ctrl->active_voice_engine_display_name);
    free(ctrl->status_icon);
    typio_state_controller_clear_mode(ctrl);
    free(ctrl->listeners);
    free(ctrl);
}

/* -------------------------------------------------------------------------- */
/* Listeners                                                                  */
/* -------------------------------------------------------------------------- */

void typio_state_controller_add_listener(TypioStateController *ctrl,
                                         TypioStateListener listener) {
    if (!ctrl) {
        return;
    }
    if (ctrl->listener_count >= ctrl->listener_capacity) {
        size_t new_cap = ctrl->listener_capacity * 2;
        TypioStateListener *new_list =
            realloc(ctrl->listeners, new_cap * sizeof(TypioStateListener));
        if (!new_list) {
            typio_log(TYPIO_LOG_ERROR,
                      "Failed to grow state-controller listener list");
            return;
        }
        ctrl->listeners = new_list;
        ctrl->listener_capacity = new_cap;
    }
    ctrl->listeners[ctrl->listener_count++] = listener;
}

void typio_state_controller_remove_listener(TypioStateController *ctrl,
                                            void *user_data) {
    if (!ctrl) {
        return;
    }
    for (size_t i = 0; i < ctrl->listener_count; i++) {
        if (ctrl->listeners[i].user_data == user_data) {
            /* shift remaining entries down */
            size_t rest = ctrl->listener_count - i - 1;
            if (rest > 0) {
                memmove(&ctrl->listeners[i],
                        &ctrl->listeners[i + 1],
                        rest * sizeof(TypioStateListener));
            }
            ctrl->listener_count--;
            return;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Queries                                                                    */
/* -------------------------------------------------------------------------- */

const char *typio_state_controller_get_active_engine_name(
    TypioStateController *ctrl) {
    return ctrl ? ctrl->active_engine_name : nullptr;
}

const char *typio_state_controller_get_active_engine_display_name(
    TypioStateController *ctrl) {
    return ctrl ? ctrl->active_engine_display_name : nullptr;
}

const char *typio_state_controller_get_active_voice_engine_name(
    TypioStateController *ctrl) {
    return ctrl ? ctrl->active_voice_engine_name : nullptr;
}

const char *typio_state_controller_get_active_voice_engine_display_name(
    TypioStateController *ctrl) {
    return ctrl ? ctrl->active_voice_engine_display_name : nullptr;
}

const char *typio_state_controller_get_status_icon(
    TypioStateController *ctrl) {
    return ctrl ? ctrl->status_icon : nullptr;
}

bool typio_state_controller_get_engine_active(
    TypioStateController *ctrl) {
    return ctrl ? ctrl->engine_active : false;
}

const TypioEngineMode *typio_state_controller_get_current_mode(
    TypioStateController *ctrl) {
    if (!ctrl || !ctrl->has_mode) {
        return nullptr;
    }
    return &ctrl->mode;
}

/* -------------------------------------------------------------------------- */
/* Core notifications                                                         */
/* -------------------------------------------------------------------------- */

void typio_state_controller_notify_engine_changed(
    TypioStateController *ctrl,
    const TypioEngineInfo *info) {
    if (!ctrl) {
        return;
    }
    free(ctrl->active_engine_name);
    free(ctrl->active_engine_display_name);
    ctrl->active_engine_name =
        (info && info->name) ? strdup(info->name) : nullptr;
    ctrl->active_engine_display_name =
        (info && info->display_name) ? strdup(info->display_name) : nullptr;

    /* Re-evaluate status icon: dynamic icon takes precedence, then the
     * engine's static icon, then the default fallback. */
    {
        free(ctrl->status_icon);
        const char *icon = typio_instance_get_last_status_icon(ctrl->instance);
        if (icon && *icon) {
            ctrl->status_icon = strdup(icon);
        } else if (info && info->icon && info->icon[0]) {
            ctrl->status_icon = strdup(info->icon);
        } else {
            ctrl->status_icon = strdup("typio-keyboard");
        }
    }

    typio_state_controller_update_engine_active(ctrl);
    typio_state_controller_broadcast(ctrl, TYPIO_STATE_CHANGE_ENGINE);
}

void typio_state_controller_notify_voice_engine_changed(
    TypioStateController *ctrl,
    const TypioEngineInfo *info) {
    if (!ctrl) {
        return;
    }
    free(ctrl->active_voice_engine_name);
    free(ctrl->active_voice_engine_display_name);
    ctrl->active_voice_engine_name =
        (info && info->name) ? strdup(info->name) : nullptr;
    ctrl->active_voice_engine_display_name =
        (info && info->display_name) ? strdup(info->display_name) : nullptr;
    typio_state_controller_broadcast(ctrl, TYPIO_STATE_CHANGE_VOICE_ENGINE);
}

void typio_state_controller_notify_mode_changed(
    TypioStateController *ctrl,
    const TypioEngineMode *mode) {
    if (!ctrl) {
        return;
    }
    typio_state_controller_set_mode(ctrl, mode);
    typio_state_controller_broadcast(ctrl, TYPIO_STATE_CHANGE_MODE);
}

void typio_state_controller_notify_status_icon_changed(
    TypioStateController *ctrl,
    const char *icon_name) {
    if (!ctrl) {
        return;
    }
    free(ctrl->status_icon);
    ctrl->status_icon = typio_state_strdup(icon_name);
    typio_state_controller_broadcast(ctrl, TYPIO_STATE_CHANGE_STATUS_ICON);
}

/* -------------------------------------------------------------------------- */
/* Sync                                                                       */
/* -------------------------------------------------------------------------- */

void typio_state_controller_sync(TypioStateController *ctrl) {
    if (!ctrl || !ctrl->instance) {
        return;
    }

    /* Engine */
    {
        TypioEngineManager *manager =
            typio_instance_get_engine_manager(ctrl->instance);
        TypioEngine *active =
            manager ? typio_engine_manager_get_active(manager) : nullptr;
        free(ctrl->active_engine_name);
        free(ctrl->active_engine_display_name);
        ctrl->active_engine_name =
            active ? typio_state_strdup(typio_engine_get_name(active)) : nullptr;
        ctrl->active_engine_display_name =
            (active && active->info && active->info->display_name)
                ? strdup(active->info->display_name)
                : nullptr;
        ctrl->engine_active = active != nullptr;
    }

    /* Voice engine */
    {
        TypioEngineManager *manager =
            typio_instance_get_engine_manager(ctrl->instance);
        TypioEngine *voice =
            manager ? typio_engine_manager_get_active_voice(manager) : nullptr;
        free(ctrl->active_voice_engine_name);
        free(ctrl->active_voice_engine_display_name);
        ctrl->active_voice_engine_name =
            voice ? typio_state_strdup(typio_engine_get_name(voice)) : nullptr;
        ctrl->active_voice_engine_display_name =
            (voice && voice->info && voice->info->display_name)
                ? strdup(voice->info->display_name)
                : nullptr;
    }

    /* Status icon */
    {
        free(ctrl->status_icon);
        const char *icon = typio_instance_get_last_status_icon(ctrl->instance);
        if (icon && *icon) {
            ctrl->status_icon = strdup(icon);
        } else {
            TypioEngineManager *manager =
                typio_instance_get_engine_manager(ctrl->instance);
            TypioEngine *active =
                manager ? typio_engine_manager_get_active(manager) : nullptr;
            if (active && active->info && active->info->icon &&
                active->info->icon[0]) {
                ctrl->status_icon = strdup(active->info->icon);
            } else {
                ctrl->status_icon = strdup("typio-keyboard");
            }
        }
    }

    /* Mode — we cannot query current mode directly from instance, so we
     * clear it and wait for the next mode notification from Core. */
    typio_state_controller_clear_mode(ctrl);

    /* Broadcast every change type so listeners perform a full refresh. */
    typio_state_controller_broadcast(ctrl, TYPIO_STATE_CHANGE_ENGINE);
    typio_state_controller_broadcast(ctrl, TYPIO_STATE_CHANGE_VOICE_ENGINE);
    typio_state_controller_broadcast(ctrl, TYPIO_STATE_CHANGE_STATUS_ICON);
}
