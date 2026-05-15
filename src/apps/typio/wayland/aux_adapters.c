/**
 * @file aux_adapters.c
 * @brief TypioWlAuxHandler adapters for status bus, tray, and voice.
 */

#include "typio_build_config.h"
#include "aux_handler.h"
#include "utils/log.h"
#include <stdlib.h>

#ifdef HAVE_STATUS_BUS
#include "status/status.h"

static int status_bus_aux_fd(void *userdata) {
    TypioStatusBus *bus = (TypioStatusBus *)userdata;
    return bus ? typio_status_bus_get_fd(bus) : -1;
}

static void status_bus_aux_ready(void *userdata) {
    TypioStatusBus *bus = (TypioStatusBus *)userdata;
    if (bus) {
        int result = typio_status_bus_dispatch(bus);
        if (result < 0) {
            typio_log(TYPIO_LOG_WARNING, "Status bus dispatch failed");
        }
    }
}

TypioWlAuxHandler *typio_wl_aux_handler_for_status_bus(TypioStatusBus *bus) {
    if (!bus) return nullptr;
    return typio_wl_aux_handler_new("status_bus",
                                     status_bus_aux_fd,
                                     status_bus_aux_ready,
                                     nullptr,
                                     bus);
}
#endif

#ifdef HAVE_SYSTRAY
#include "tray/tray.h"

static int tray_aux_fd(void *userdata) {
    TypioTray *tray = (TypioTray *)userdata;
    return tray ? typio_tray_get_fd(tray) : -1;
}

static void tray_aux_ready(void *userdata) {
    TypioTray *tray = (TypioTray *)userdata;
    if (tray) {
        int result = typio_tray_dispatch(tray);
        if (result < 0) {
            typio_log(TYPIO_LOG_WARNING, "Tray dispatch failed");
        }
    }
}

TypioWlAuxHandler *typio_wl_aux_handler_for_tray(TypioTray *tray) {
    if (!tray) return nullptr;
    return typio_wl_aux_handler_new("tray",
                                     tray_aux_fd,
                                     tray_aux_ready,
                                     nullptr,
                                     tray);
}
#endif

#ifdef HAVE_VOICE
#include "voice/voice_service.h"
#include "wl_frontend_internal.h"

typedef struct {
    TypioVoiceService *voice;
    TypioWlFrontend *frontend;
} VoiceAuxData;

static int voice_aux_fd(void *userdata) {
    VoiceAuxData *d = (VoiceAuxData *)userdata;
    return d ? typio_voice_service_get_fd(d->voice) : -1;
}

static void voice_aux_ready(void *userdata) {
    VoiceAuxData *d = (VoiceAuxData *)userdata;
    if (!d || !d->voice || !d->frontend) return;

    TypioInputContext *ctx = (d->frontend->session)
                             ? d->frontend->session->ctx : nullptr;

    char *text = typio_voice_service_collect(d->voice);

    /* Always clear [Processing...] preedit regardless of transcription outcome */
    typio_wl_set_preedit(d->frontend, "", 0, 0);

    if (text && ctx) {
        const char *p = text;
        while (*p == ' ') p++;
        if (*p != '\0') {
            typio_log(TYPIO_LOG_INFO, "Voice result: \"%s\"", p);
            typio_wl_commit_string(d->frontend, p);
        } else {
            typio_log(TYPIO_LOG_INFO, "Voice result: (empty after trim)");
        }
    } else if (!text || !text[0]) {
        typio_log(TYPIO_LOG_INFO, "Voice result: (empty)");
    } else {
        typio_log(TYPIO_LOG_WARNING,
                  "Voice result discarded: no active input context");
    }

    typio_wl_commit(d->frontend);
    free(text);
}

static void voice_aux_free(void *userdata) {
    free(userdata);
}

TypioWlAuxHandler *typio_wl_aux_handler_for_voice(TypioVoiceService *voice,
                                                    TypioWlFrontend *frontend) {
    if (!voice) return nullptr;
    VoiceAuxData *d = calloc(1, sizeof(VoiceAuxData));
    if (!d) return nullptr;
    d->voice = voice;
    d->frontend = frontend;
    TypioWlAuxHandler *h = typio_wl_aux_handler_new("voice",
                                                      voice_aux_fd,
                                                      voice_aux_ready,
                                                      voice_aux_free,
                                                      d);
    if (!h) free(d);
    return h;
}
#endif
