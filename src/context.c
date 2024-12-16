#include "context.h"
#include "utils.h"
#include <string.h>

#define MIN_BUFFER_SIZE 32

IMContext* im_init(size_t initial_capacity) {
    if (initial_capacity < MIN_BUFFER_SIZE) {
        initial_capacity = MIN_BUFFER_SIZE;
    }

    IMContext* ctx = im_malloc(sizeof(IMContext));
    if (!ctx) return NULL;

    ctx->buffer = im_malloc(initial_capacity);
    if (!ctx->buffer) {
        im_free(ctx);
        return NULL;
    }

    ctx->buffer_size = 0;
    ctx->buffer_capacity = initial_capacity;
    ctx->cursor_pos = 0;
    ctx->is_composing = false;
    ctx->buffer[0] = '\0';

    return ctx;
}

static bool handle_special_key(IMContext* ctx, const IMKeyEvent* event) {
    switch (event->code) {
        case IM_KEY_BACKSPACE:
            if (ctx->cursor_pos > 0) {
                memmove(&ctx->buffer[ctx->cursor_pos - 1], 
                        &ctx->buffer[ctx->cursor_pos],
                        ctx->buffer_size - ctx->cursor_pos);
                ctx->buffer_size--;
                ctx->cursor_pos--;
                ctx->buffer[ctx->buffer_size] = '\0';
                return true;
            }
            break;

        case IM_KEY_DELETE:
            if (ctx->cursor_pos < ctx->buffer_size) {
                memmove(&ctx->buffer[ctx->cursor_pos],
                        &ctx->buffer[ctx->cursor_pos + 1],
                        ctx->buffer_size - ctx->cursor_pos - 1);
                ctx->buffer_size--;
                ctx->buffer[ctx->buffer_size] = '\0';
                return true;
            }
            break;

        case IM_KEY_LEFT:
            if (ctx->cursor_pos > 0) {
                ctx->cursor_pos--;
                return true;
            }
            break;

        case IM_KEY_RIGHT:
            if (ctx->cursor_pos < ctx->buffer_size) {
                ctx->cursor_pos++;
                return true;
            }
            break;

        case IM_KEY_HOME:
            if (ctx->cursor_pos > 0) {
                ctx->cursor_pos = 0;
                return true;
            }
            break;

        case IM_KEY_END:
            if (ctx->cursor_pos < ctx->buffer_size) {
                ctx->cursor_pos = ctx->buffer_size;
                return true;
            }
            break;

        default:
            break;
    }
    return false;
}

bool im_process_key_event(IMContext* ctx, const IMKeyEvent* event) {
    if (!ctx || !event) return false;

    // Handle only key press events for now
    if (event->type != IM_KEY_PRESS) return false;

    // Handle special keys
    if (!im_key_event_is_printable(event)) {
        return handle_special_key(ctx, event);
    }

    // Handle printable characters
    if (ctx->buffer_size + 1 >= ctx->buffer_capacity) {
        size_t new_capacity = ctx->buffer_capacity * 2;
        char* new_buffer = im_realloc(ctx->buffer, new_capacity);
        if (!new_buffer) return false;
        
        ctx->buffer = new_buffer;
        ctx->buffer_capacity = new_capacity;
    }

    // Insert character at cursor position
    memmove(&ctx->buffer[ctx->cursor_pos + 1],
            &ctx->buffer[ctx->cursor_pos],
            ctx->buffer_size - ctx->cursor_pos);
    
    ctx->buffer[ctx->cursor_pos] = event->character;
    ctx->buffer_size++;
    ctx->cursor_pos++;
    ctx->buffer[ctx->buffer_size] = '\0';
    ctx->is_composing = true;

    return true;
}

const char* im_get_composition(const IMContext* ctx) {
    return ctx ? ctx->buffer : NULL;
}

void im_clear_composition(IMContext* ctx) {
    if (ctx) {
        ctx->buffer_size = 0;
        ctx->cursor_pos = 0;
        ctx->buffer[0] = '\0';
        ctx->is_composing = false;
    }
}

size_t im_get_cursor_pos(const IMContext* ctx) {
    return ctx ? ctx->cursor_pos : 0;
}

bool im_set_cursor_pos(IMContext* ctx, size_t pos) {
    if (!ctx || pos > ctx->buffer_size) return false;
    ctx->cursor_pos = pos;
    return true;
}

void im_destroy(IMContext* ctx) {
    if (ctx) {
        im_free(ctx->buffer);
        im_free(ctx);
    }
} 