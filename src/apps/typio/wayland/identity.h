/**
 * @file identity.h
 * @brief Focused-application identity providers and per-identity engine memory
 */

#ifndef TYPIO_WL_IDENTITY_H
#define TYPIO_WL_IDENTITY_H

#include "wl_frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypioWlIdentityProvider TypioWlIdentityProvider;

typedef struct TypioWlIdentity {
    char *provider_name;
    char *app_id;
    char *stable_key;
} TypioWlIdentity;

TypioWlIdentityProvider *typio_wl_identity_provider_new(TypioInstance *instance);
void typio_wl_identity_provider_free(TypioWlIdentityProvider *provider);
const char *typio_wl_identity_provider_name(TypioWlIdentityProvider *provider);
bool typio_wl_identity_provider_query_current(TypioWlIdentityProvider *provider,
                                              TypioWlIdentity *identity);
bool typio_wl_identity_parse_niri_focused_window(const char *response,
                                                 TypioWlIdentity *identity);

void typio_wl_identity_clear(TypioWlIdentity *identity);

void typio_wl_frontend_refresh_identity(TypioWlFrontend *frontend);
void typio_wl_frontend_clear_identity(TypioWlFrontend *frontend);
void typio_wl_frontend_restore_identity_engine(TypioWlFrontend *frontend);
void typio_wl_frontend_remember_active_engine(TypioWlFrontend *frontend,
                                              const char *engine_name);
void typio_wl_frontend_remember_active_mode(TypioWlFrontend *frontend,
                                            const char *engine_name,
                                            const char *mode_id);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_IDENTITY_H */
