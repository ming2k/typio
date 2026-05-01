/**
 * @file aux_adapters.h
 * @brief Convenience constructors that wrap Typio subsystems in TypioWlAuxHandler.
 */

#ifndef TYPIO_WL_AUX_ADAPTERS_H
#define TYPIO_WL_AUX_ADAPTERS_H

#include "aux_handler.h"

#ifdef HAVE_STATUS_BUS
#include "status/status.h"
TypioWlAuxHandler *typio_wl_aux_handler_for_status_bus(TypioStatusBus *bus);
#endif

#ifdef HAVE_SYSTRAY
#include "tray/tray.h"
TypioWlAuxHandler *typio_wl_aux_handler_for_tray(TypioTray *tray);
#endif

#ifdef HAVE_VOICE
#include "voice/voice_service.h"
TypioWlAuxHandler *typio_wl_aux_handler_for_voice(TypioVoiceService *voice);
#endif

#endif /* TYPIO_WL_AUX_ADAPTERS_H */
