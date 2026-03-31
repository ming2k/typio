#ifndef TYPIO_BUILD_INFO_H
#define TYPIO_BUILD_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

const char *typio_build_version(void);
const char *typio_build_source_label(void);
const char *typio_build_display_string(void);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_BUILD_INFO_H */
