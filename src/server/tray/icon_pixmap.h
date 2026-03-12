/**
 * @file icon_pixmap.h
 * @brief Pixmap fallback builder for StatusNotifierItem hosts
 */

#ifndef TYPIO_TRAY_ICON_PIXMAP_H
#define TYPIO_TRAY_ICON_PIXMAP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool typio_tray_icon_pixmap_build(const char *icon_name, int preferred_size,
                                  int *width_out, int *height_out,
                                  unsigned char **data_out, int *data_len_out);
void typio_tray_icon_pixmap_free(unsigned char *data);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_TRAY_ICON_PIXMAP_H */
