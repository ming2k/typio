#ifndef TYPIO_WL_KEY_DEBUG_H
#define TYPIO_WL_KEY_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void typio_wl_key_debug_format(uint32_t unicode, char *buffer, size_t size);
void typio_wl_key_debug_format_keysym(uint32_t keysym, char *buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_KEY_DEBUG_H */
