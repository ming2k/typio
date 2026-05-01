/**
 * @file result.c
 * @brief TypioResult helper implementations
 */

#include "result.h"

const char *typio_result_to_string(TypioResult result) {
    switch (result) {
    case TYPIO_OK:                      return "OK";
    case TYPIO_ERROR:                   return "Generic error";
    case TYPIO_ERROR_INVALID_ARGUMENT:  return "Invalid argument";
    case TYPIO_ERROR_OUT_OF_MEMORY:     return "Out of memory";
    case TYPIO_ERROR_NOT_FOUND:         return "Not found";
    case TYPIO_ERROR_ALREADY_EXISTS:    return "Already exists";
    case TYPIO_ERROR_NOT_INITIALIZED:   return "Not initialized";
    case TYPIO_ERROR_ENGINE_LOAD_FAILED: return "Engine load failed";
    case TYPIO_ERROR_ENGINE_NOT_AVAILABLE: return "Engine not available";
    default:                            return "Unknown error";
    }
}
