/**
 * @file mock_compositor.h
 * @brief Minimal headless Wayland compositor for integration tests.
 *
 * This mock implements just enough of the input-method/text-input protocol
 * to exercise a full TypioWlFrontend activate→grab→key→commit cycle without
 * a real compositor.
 */

#ifndef TYPIO_MOCK_COMPOSITOR_H
#define TYPIO_MOCK_COMPOSITOR_H

#include "typio/types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MockCompositor MockCompositor;

MockCompositor *mock_compositor_create(void);
void            mock_compositor_destroy(MockCompositor *mc);

const char     *mock_compositor_get_socket(MockCompositor *mc);

/* Simulate compositor → IM events */
void mock_compositor_activate(MockCompositor *mc);
void mock_compositor_deactivate(MockCompositor *mc);
void mock_compositor_send_key(MockCompositor *mc, uint32_t keycode, bool pressed);
void mock_compositor_send_surrounding_text(MockCompositor *mc,
                                            const char *text,
                                            uint32_t cursor,
                                            uint32_t anchor);
void mock_compositor_done(MockCompositor *mc);

/* Query state sent IM → compositor */
const char *mock_compositor_get_last_preedit(MockCompositor *mc);
const char *mock_compositor_get_last_committed_text(MockCompositor *mc);
int         mock_compositor_get_preedit_cursor(MockCompositor *mc);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_MOCK_COMPOSITOR_H */
