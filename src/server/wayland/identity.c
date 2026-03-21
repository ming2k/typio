/**
 * @file identity.c
 * @brief Focused-application identity providers and per-identity engine memory
 */

#include "identity.h"

#include "wl_frontend_internal.h"
#include "typio/config.h"
#include "typio/engine.h"
#include "typio/engine_manager.h"
#include "typio/instance.h"
#include "utils/log.h"
#include "utils/string.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define TYPIO_IDENTITY_STATE_FILE "identity-engine-state.toml"

typedef struct TypioWlIdentityProviderOps {
    const char *name;
    bool (*query_current)(struct TypioWlIdentityProvider *provider,
                          TypioWlIdentity *identity);
} TypioWlIdentityProviderOps;

struct TypioWlIdentityProvider {
    TypioInstance *instance;
    const TypioWlIdentityProviderOps *ops;
    char *niri_socket_path;
};

static char *identity_state_path(TypioInstance *instance) {
    const char *state_dir;

    if (!instance)
        return nullptr;

    state_dir = typio_instance_get_state_dir(instance);
    if (!state_dir || !*state_dir)
        return nullptr;

    return typio_path_join(state_dir, TYPIO_IDENTITY_STATE_FILE);
}

static char *identity_hex_encode(const char *text) {
    static const char hex_digits[] = "0123456789abcdef";
    size_t len;
    char *encoded;

    if (!text)
        return nullptr;

    len = strlen(text);
    encoded = calloc(len * 2 + 1, sizeof(char));
    if (!encoded)
        return nullptr;

    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)text[i];
        encoded[i * 2] = hex_digits[(ch >> 4) & 0x0f];
        encoded[i * 2 + 1] = hex_digits[ch & 0x0f];
    }

    return encoded;
}

static char *identity_config_key(const TypioWlIdentity *identity) {
    char *encoded;
    char *key;

    if (!identity || !identity->stable_key || !*identity->stable_key)
        return nullptr;

    encoded = identity_hex_encode(identity->stable_key);
    if (!encoded)
        return nullptr;

    key = typio_strjoin3("identities.", encoded, "");
    free(encoded);
    return key;
}

static char *identity_memory_load_engine(TypioInstance *instance,
                                         const TypioWlIdentity *identity) {
    TypioConfig *config;
    char *path;
    char *key;
    const char *engine_name;
    char *copy = nullptr;

    path = identity_state_path(instance);
    key = identity_config_key(identity);
    if (!path || !key) {
        free(path);
        free(key);
        return nullptr;
    }

    config = typio_config_load_file(path);
    if (config) {
        engine_name = typio_config_get_string(config, key, NULL);
        copy = engine_name && *engine_name ? typio_strdup(engine_name) : nullptr;
        typio_config_free(config);
    }

    free(path);
    free(key);
    return copy;
}

static void identity_memory_store_engine(TypioInstance *instance,
                                         const TypioWlIdentity *identity,
                                         const char *engine_name) {
    TypioConfig *config;
    char *path;
    char *key;

    if (!instance || !identity || !engine_name || !*engine_name)
        return;

    path = identity_state_path(instance);
    key = identity_config_key(identity);
    if (!path || !key) {
        free(path);
        free(key);
        return;
    }

    config = typio_config_load_file(path);
    if (!config)
        config = typio_config_new();
    if (!config) {
        free(path);
        free(key);
        return;
    }

    typio_config_set_string(config, key, engine_name);
    typio_config_save_file(config, path);
    typio_config_free(config);
    free(path);
    free(key);
}

static char *json_find_string_value(const char *json, const char *field_name) {
    char pattern[128];
    const char *cursor;
    const char *start;
    char *result;
    size_t out = 0;
    bool escaped = false;

    if (!json || !field_name || !*field_name)
        return nullptr;

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", field_name);
    cursor = strstr(json, pattern);
    if (!cursor)
        return nullptr;

    start = cursor + strlen(pattern);
    result = calloc(strlen(start) + 1, sizeof(char));
    if (!result)
        return nullptr;

    for (const char *p = start; *p; ++p) {
        char ch = *p;

        if (escaped) {
            switch (ch) {
            case 'n':
                result[out++] = '\n';
                break;
            case 'r':
                result[out++] = '\r';
                break;
            case 't':
                result[out++] = '\t';
                break;
            case '\\':
            case '"':
            case '/':
                result[out++] = ch;
                break;
            default:
                result[out++] = ch;
                break;
            }
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        if (ch == '"') {
            result[out] = '\0';
            return result;
        }

        result[out++] = ch;
    }

    free(result);
    return nullptr;
}

static bool niri_socket_request(const char *socket_path,
                                const char *request,
                                char *response,
                                size_t response_size) {
    int fd;
    struct sockaddr_un addr = {};
    size_t request_len;
    size_t written = 0;
    size_t total = 0;

    if (!socket_path || !*socket_path || !request || !response || response_size == 0)
        return false;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        typio_log(TYPIO_LOG_WARNING,
                  "Failed to create niri IPC socket: errno=%d",
                  errno);
        return false;
    }

    addr.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(addr.sun_path)) {
        typio_log(TYPIO_LOG_WARNING,
                  "Niri socket path too long: %s",
                  socket_path);
        close(fd);
        return false;
    }
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);

    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        typio_log(TYPIO_LOG_WARNING,
                  "Failed to connect to niri IPC socket %s: errno=%d",
                  socket_path, errno);
        close(fd);
        return false;
    }

    request_len = strlen(request);
    while (written < request_len) {
        ssize_t rv = write(fd, request + written, request_len - written);
        if (rv <= 0) {
            typio_log(TYPIO_LOG_WARNING,
                      "Failed to write niri IPC request: errno=%d",
                      errno);
            close(fd);
            return false;
        }
        written += (size_t)rv;
    }

    shutdown(fd, SHUT_WR);

    while (total + 1 < response_size) {
        ssize_t rv = read(fd, response + total, response_size - total - 1);
        if (rv < 0) {
            if (errno == EINTR)
                continue;
            typio_log(TYPIO_LOG_WARNING,
                      "Failed to read niri IPC response: errno=%d",
                      errno);
            close(fd);
            return false;
        }
        if (rv == 0)
            break;
        total += (size_t)rv;
        if (memchr(response, '\n', total) != NULL)
            break;
    }

    response[total] = '\0';
    close(fd);
    return total > 0;
}

bool typio_wl_identity_parse_niri_focused_window(const char *response,
                                                 TypioWlIdentity *identity) {
    char *app_id;

    if (!response || !identity)
        return false;

    memset(identity, 0, sizeof(*identity));
    app_id = json_find_string_value(response, "app_id");
    if (!app_id || !*app_id) {
        typio_log(TYPIO_LOG_WARNING,
                  "Niri identity response did not contain app_id: %s",
                  response);
        free(app_id);
        return false;
    }

    identity->provider_name = typio_strdup("niri");
    identity->app_id = app_id;
    identity->stable_key = typio_strjoin3("niri:", app_id, "");
    if (!identity->provider_name || !identity->stable_key) {
        typio_wl_identity_clear(identity);
        return false;
    }

    return true;
}

static bool niri_identity_query(struct TypioWlIdentityProvider *provider,
                                TypioWlIdentity *identity) {
    char response[4096];

    if (!provider || !provider->niri_socket_path || !identity)
        return false;

    if (!niri_socket_request(provider->niri_socket_path,
                             "\"FocusedWindow\"\n",
                             response, sizeof(response))) {
        typio_log(TYPIO_LOG_WARNING,
                  "Niri identity query failed for socket %s",
                  provider->niri_socket_path);
        return false;
    }

    return typio_wl_identity_parse_niri_focused_window(response, identity);
}

static const TypioWlIdentityProviderOps niri_identity_provider_ops = {
    .name = "niri",
    .query_current = niri_identity_query,
};

TypioWlIdentityProvider *typio_wl_identity_provider_new(TypioInstance *instance) {
    TypioWlIdentityProvider *provider;
    const char *niri_socket;

    provider = calloc(1, sizeof(*provider));
    if (!provider)
        return nullptr;

    provider->instance = instance;

    niri_socket = getenv("NIRI_SOCKET");
    if (niri_socket && *niri_socket) {
        provider->ops = &niri_identity_provider_ops;
        provider->niri_socket_path = typio_strdup(niri_socket);
        if (!provider->niri_socket_path) {
            free(provider);
            return nullptr;
        }
        typio_log(TYPIO_LOG_INFO, "Focused-app identity provider enabled: niri");
    }

    return provider;
}

void typio_wl_identity_provider_free(TypioWlIdentityProvider *provider) {
    if (!provider)
        return;

    free(provider->niri_socket_path);
    free(provider);
}

const char *typio_wl_identity_provider_name(TypioWlIdentityProvider *provider) {
    return provider && provider->ops ? provider->ops->name : "none";
}

bool typio_wl_identity_provider_query_current(TypioWlIdentityProvider *provider,
                                              TypioWlIdentity *identity) {
    if (!provider || !provider->ops || !provider->ops->query_current || !identity)
        return false;

    memset(identity, 0, sizeof(*identity));
    return provider->ops->query_current(provider, identity);
}

void typio_wl_identity_clear(TypioWlIdentity *identity) {
    if (!identity)
        return;

    free(identity->provider_name);
    free(identity->app_id);
    free(identity->stable_key);
    memset(identity, 0, sizeof(*identity));
}

void typio_wl_frontend_clear_identity(TypioWlFrontend *frontend) {
    if (!frontend)
        return;

    typio_wl_identity_clear(&frontend->current_identity);
}

void typio_wl_frontend_refresh_identity(TypioWlFrontend *frontend) {
    TypioWlIdentity identity = {};

    if (!frontend)
        return;

    typio_wl_frontend_clear_identity(frontend);
    if (!frontend->identity_provider)
        return;

    if (!typio_wl_identity_provider_query_current(frontend->identity_provider,
                                                  &identity)) {
        typio_log(TYPIO_LOG_DEBUG,
                  "No focused-app identity available from provider %s",
                  typio_wl_identity_provider_name(frontend->identity_provider));
        return;
    }

    frontend->current_identity = identity;
    typio_log(TYPIO_LOG_INFO,
              "Focused app identity: provider=%s app_id=%s",
              frontend->current_identity.provider_name,
              frontend->current_identity.app_id);
}

void typio_wl_frontend_restore_identity_engine(TypioWlFrontend *frontend) {
    TypioEngineManager *manager;
    TypioEngine *active;
    char *engine_name;

    if (!frontend || !frontend->instance || !frontend->current_identity.stable_key)
        return;

    engine_name = identity_memory_load_engine(frontend->instance,
                                              &frontend->current_identity);
    if (!engine_name || !*engine_name) {
        free(engine_name);
        return;
    }

    manager = typio_instance_get_engine_manager(frontend->instance);
    active = manager ? typio_engine_manager_get_active(manager) : nullptr;
    if (active && typio_str_equals(typio_engine_get_name(active), engine_name)) {
        free(engine_name);
        return;
    }

    if (manager && typio_engine_manager_set_active(manager, engine_name) == TYPIO_OK) {
        typio_log(TYPIO_LOG_INFO,
                  "Restored keyboard engine %s for %s",
                  engine_name,
                  frontend->current_identity.stable_key);
    }

    free(engine_name);
}

void typio_wl_frontend_remember_active_engine(TypioWlFrontend *frontend,
                                              const char *engine_name) {
    if (!frontend || !frontend->instance || !engine_name || !*engine_name ||
        !frontend->current_identity.stable_key) {
        return;
    }

    identity_memory_store_engine(frontend->instance,
                                 &frontend->current_identity,
                                 engine_name);
    typio_log(TYPIO_LOG_INFO,
              "Remembered keyboard engine %s for %s",
              engine_name,
              frontend->current_identity.stable_key);
}
