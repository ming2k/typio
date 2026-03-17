#ifndef TYPIO_BUILD_CONFIG_H
#define TYPIO_BUILD_CONFIG_H

#define TYPIO_VERSION "2.0.1"
#define TYPIO_VERSION_MAJOR 2
#define TYPIO_VERSION_MINOR 0
#define TYPIO_VERSION_PATCH 1

#define TYPIO_INSTALL_PREFIX "/usr/local"
#define TYPIO_ENGINE_DIR "/usr/local/lib/typio/engines"
#define TYPIO_DATA_DIR "/usr/local/share/typio"
#define TYPIO_INSTALL_ICON_DIR "/usr/local/share/icons"
#define TYPIO_SOURCE_ICON_DIR "/home/ming/projects/typio/data/icons"

#define BUILD_BASIC_ENGINE
#define BUILD_RIME_ENGINE
/* #undef BUILD_MOZC_ENGINE */
#define HAVE_WAYLAND
#define HAVE_STATUS_BUS
#define HAVE_SYSTRAY
/* #undef HAVE_VOICE */
/* #undef HAVE_WHISPER */
/* #undef HAVE_SHERPA_ONNX */

#endif /* TYPIO_BUILD_CONFIG_H */
