/**
 * @file aux_adapters.c
 * @brief TypioWlAuxHandler adapters for status bus, tray, and voice.
 */

#include "aux_handler.h"
#include "utils/log.h"

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

static int voice_aux_fd(void *userdata) {
    TypioVoiceService *voice = (TypioVoiceService *)userdata;
    return voice ? typio_voice_service_get_fd(voice) : -1;
}

static void voice_aux_ready(void *userdata) {
    TypioVoiceService *voice = (TypioVoiceService *)userdata;
    if (voice) {
        /* Voice dispatch requires the current input context */
        /* We stash the frontend pointer in userdata via a wrapper */
        typio_voice_service_dispatch(voice, nullptr);
    }
}

TypioWlAuxHandler *typio_wl_aux_handler_for_voice(TypioVoiceService *voice) {
    if (!voice) return nullptr;
    return typio_wl_aux_handler_new("voice",
                                     voice_aux_fd,
                                     voice_aux_ready,
                                     nullptr,
                                     voice);
}
#endif
