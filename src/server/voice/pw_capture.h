/**
 * @file pw_capture.h
 * @brief PipeWire audio capture for voice input
 */

#ifndef TYPIO_PW_CAPTURE_H
#define TYPIO_PW_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TypioPwCaptureCallback)(const float *samples, size_t count,
                                       void *user_data);

typedef struct TypioPwCapture TypioPwCapture;

TypioPwCapture *typio_pw_capture_new(TypioPwCaptureCallback cb,
                                     void *user_data);
void typio_pw_capture_free(TypioPwCapture *cap);
bool typio_pw_capture_start(TypioPwCapture *cap);
void typio_pw_capture_stop(TypioPwCapture *cap);
int typio_pw_capture_get_fd(TypioPwCapture *cap);
void typio_pw_capture_dispatch(TypioPwCapture *cap);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_PW_CAPTURE_H */
