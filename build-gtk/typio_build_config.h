#ifndef TYPIO_BUILD_CONFIG_H
#define TYPIO_BUILD_CONFIG_H

#define TYPIO_VERSION "1.1.0"
#define TYPIO_VERSION_MAJOR 1
#define TYPIO_VERSION_MINOR 1
#define TYPIO_VERSION_PATCH 0

#define TYPIO_INSTALL_PREFIX "/usr/local"
#define TYPIO_ENGINE_DIR "/usr/local/lib/typio/engines"
#define TYPIO_DATA_DIR "/usr/local/share/typio"
#define TYPIO_INSTALL_ICON_DIR "/usr/local/share/icons"
#define TYPIO_SOURCE_ICON_DIR "/home/ming/projects/typio/data/icons"

#define BUILD_BASIC_ENGINE
#define BUILD_RIME_ENGINE
#define HAVE_WAYLAND
#define HAVE_STATUS_BUS
#define HAVE_SYSTRAY

#endif /* TYPIO_BUILD_CONFIG_H */
