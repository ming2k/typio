/**
 * @file engine_label.h
 * @brief Shared display labels for engine identifiers.
 */

#ifndef TYPIO_ENGINE_LABEL_H
#define TYPIO_ENGINE_LABEL_H

#include "engine.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *typio_engine_label_fallback(const char *engine_name);
const char *typio_engine_label_from_info(const TypioEngineInfo *info);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_ENGINE_LABEL_H */
