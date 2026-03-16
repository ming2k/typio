/**
 * @file health.c
 * @brief Startup health checks for desktop notifications
 */

#include "health.h"

#include "typio/config.h"
#include "typio/engine_manager.h"
#include "typio/instance.h"
#include "typio_build_config.h"

#include <stdio.h>
#include <string.h>

static bool startup_setting(const TypioConfig *config,
                            const char *key,
                            bool default_value) {
    if (!config) {
        return default_value;
    }
    return typio_config_get_bool(config, key, default_value);
}

static uint64_t startup_int_setting(const TypioConfig *config,
                                    const char *key,
                                    uint64_t default_value) {
    int value;

    if (!config) {
        return default_value;
    }

    value = typio_config_get_int(config, key, (int)default_value);
    if (value < 0) {
        return default_value;
    }
    return (uint64_t)value;
}

static void append_issue(TypioStartupIssue *issues,
                         size_t capacity,
                         size_t *count,
                         TypioStartupIssueSeverity severity,
                         const char *code,
                         const char *title,
                         const char *body) {
    TypioStartupIssue *issue;

    if (!count) {
        return;
    }

    if (*count >= capacity || !issues) {
        (*count)++;
        return;
    }

    issue = &issues[*count];
    memset(issue, 0, sizeof(*issue));
    issue->severity = severity;
    snprintf(issue->code, sizeof(issue->code), "%s", code ? code : "");
    snprintf(issue->title, sizeof(issue->title), "%s", title ? title : "");
    snprintf(issue->body, sizeof(issue->body), "%s", body ? body : "");
    (*count)++;
}

bool typio_startup_notifications_enabled(TypioInstance *instance) {
    TypioConfig *config = typio_instance_get_config(instance);
    return startup_setting(config, "notifications.enable", true);
}

bool typio_notifications_enabled(TypioInstance *instance) {
    return typio_startup_notifications_enabled(instance);
}

bool typio_startup_checks_enabled(TypioInstance *instance) {
    TypioConfig *config = typio_instance_get_config(instance);
    if (!startup_setting(config, "notifications.enable", true)) {
        return false;
    }
    return startup_setting(config, "notifications.startup_checks", true);
}

bool typio_runtime_notifications_enabled(TypioInstance *instance) {
    TypioConfig *config = typio_instance_get_config(instance);
    if (!startup_setting(config, "notifications.enable", true)) {
        return false;
    }
    return startup_setting(config, "notifications.runtime", true);
}

bool typio_voice_notifications_enabled(TypioInstance *instance) {
    TypioConfig *config = typio_instance_get_config(instance);
    if (!startup_setting(config, "notifications.enable", true)) {
        return false;
    }
    if (!startup_setting(config, "notifications.runtime", true)) {
        return false;
    }
    return startup_setting(config, "notifications.voice", true);
}

uint64_t typio_notification_cooldown_ms(TypioInstance *instance,
                                        uint64_t default_value) {
    TypioConfig *config = typio_instance_get_config(instance);
    return startup_int_setting(config, "notifications.cooldown_ms", default_value);
}

size_t typio_startup_health_collect(TypioInstance *instance,
                                    TypioStartupIssue *issues,
                                    size_t capacity) {
    TypioConfig *config;
    TypioEngineManager *manager;
    TypioEngine *active_keyboard;
    const char *default_engine;
    size_t count = 0;

    if (!instance) {
        return 0;
    }

    config = typio_instance_get_config(instance);
    manager = typio_instance_get_engine_manager(instance);
    if (!manager) {
        append_issue(issues, capacity, &count, TYPIO_STARTUP_ISSUE_ERROR,
                     "engine-manager-missing",
                     "Typio startup incomplete",
                     "Engine manager is unavailable, so no input engine can be activated.");
        return count;
    }

    default_engine = config ? typio_config_get_string(config, "default_engine", nullptr)
                            : nullptr;
    active_keyboard = typio_engine_manager_get_active(manager);

    if (default_engine && *default_engine) {
        const TypioEngineInfo *configured_info =
            typio_engine_manager_get_info(manager, default_engine);
        if (!configured_info) {
            char body[384];
            snprintf(body, sizeof(body),
                     "Configured default engine \"%s\" is not available. Check "
                     "`default_engine` in typio.toml and installed engine plugins.",
                     default_engine);
            append_issue(issues, capacity, &count, TYPIO_STARTUP_ISSUE_WARNING,
                         "default-engine-missing",
                         "Configured keyboard engine is unavailable",
                         body);
        } else if (!active_keyboard ||
                   strcmp(typio_engine_get_name(active_keyboard), default_engine) != 0) {
            char body[384];
            snprintf(body, sizeof(body),
                     "Typio could not activate configured `default_engine = \"%s\"`."
                     "%s",
                     default_engine,
                     active_keyboard
                        ? " Another keyboard engine was activated instead."
                        : " No keyboard engine is active.");
            append_issue(issues, capacity, &count, TYPIO_STARTUP_ISSUE_WARNING,
                         "default-engine-not-active",
                         "Configured keyboard engine was not activated",
                         body);
        }
    }

    if (!active_keyboard) {
        append_issue(issues, capacity, &count, TYPIO_STARTUP_ISSUE_ERROR,
                     "no-active-keyboard-engine",
                     "No keyboard engine is active",
                     "Typio started without an active keyboard engine. Check "
                     "your engine build/install state and typio.toml.");
    }

    if (config) {
        /* Legacy voice.backend is already migrated to default_voice_engine
         * by the schema registry. */
        const char *configured_voice =
            typio_config_get_string(config, "default_voice_engine", nullptr);

        if (configured_voice && *configured_voice) {
#ifdef HAVE_VOICE
            const TypioEngineInfo *voice_info =
                typio_engine_manager_get_info(manager, configured_voice);
            TypioEngine *active_voice = typio_engine_manager_get_active_voice(manager);

            if (!voice_info) {
                char body[384];
                snprintf(body, sizeof(body),
                         "Configured voice engine \"%s\" is not available. Check "
                         "`default_voice_engine` and whether that backend was built.",
                         configured_voice);
                append_issue(issues, capacity, &count, TYPIO_STARTUP_ISSUE_WARNING,
                             "voice-engine-missing",
                             "Configured voice engine is unavailable",
                             body);
            } else if (!active_voice ||
                       strcmp(typio_engine_get_name(active_voice), configured_voice) != 0) {
                char body[384];
                snprintf(body, sizeof(body),
                         "Typio could not activate configured `default_voice_engine = \"%s\"`.",
                         configured_voice);
                append_issue(issues, capacity, &count, TYPIO_STARTUP_ISSUE_WARNING,
                             "voice-engine-not-active",
                             "Configured voice engine was not activated",
                             body);
            }
#else
            char body[384];
            snprintf(body, sizeof(body),
                     "Configured `default_voice_engine = \"%s\"`, but this build "
                     "does not include voice input support.",
                     configured_voice);
            append_issue(issues, capacity, &count, TYPIO_STARTUP_ISSUE_WARNING,
                         "voice-support-not-built",
                         "Voice input is configured but unavailable",
                         body);
#endif
        }
    }

    return count;
}
