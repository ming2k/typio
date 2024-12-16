#ifndef INPUT_METHOD_CONTEXT_H
#define INPUT_METHOD_CONTEXT_H

#include "key_event.h"
#include <stdbool.h>
#include <stddef.h>

// Input method context structure
typedef struct {
    char* buffer;              // Input buffer
    size_t buffer_size;        // Current buffer size
    size_t buffer_capacity;    // Maximum buffer capacity
    size_t cursor_pos;         // Current cursor position
    bool is_composing;         // Composition state
} IMContext;

// Initialize input method context
IMContext* im_init(size_t initial_capacity);

// Process key event
bool im_process_key_event(IMContext* ctx, const IMKeyEvent* event);

// Get current composition string
const char* im_get_composition(const IMContext* ctx);

// Clear current composition
void im_clear_composition(IMContext* ctx);

// Get cursor position
size_t im_get_cursor_pos(const IMContext* ctx);

// Set cursor position
bool im_set_cursor_pos(IMContext* ctx, size_t pos);

// Free input method context
void im_destroy(IMContext* ctx);

#endif // INPUT_METHOD_CONTEXT_H 