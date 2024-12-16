#include "input_method/key_event.h"
#include "input_method/utils.h"
#include <time.h>
#include <sys/time.h>

// Get current timestamp in milliseconds
static uint32_t get_timestamp_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
}

IMKeyEvent* im_key_event_create(IMKeyCode code, char character, IMKeyEventType type, uint32_t modifiers) {
    return im_key_event_create_ex(code, character, type, modifiers, 
                                 IM_KEY_STATE_DOWN, IM_KEY_FLAG_NONE, 0);
}

IMKeyEvent* im_key_event_create_ex(
    IMKeyCode code,
    char character,
    IMKeyEventType type,
    uint32_t modifiers,
    IMKeyState state,
    uint32_t flags,
    uint32_t scancode
) {
    IMKeyEvent* event = im_malloc(sizeof(IMKeyEvent));
    if (!event) return NULL;

    event->code = code;
    event->character = character;
    event->type = type;
    event->modifiers = modifiers;
    event->timestamp = get_timestamp_ms();
    event->state = state;
    event->flags = flags;
    event->scancode = scancode;
    event->repeat_count = 0;

    return event;
}

bool im_key_event_has_modifier(const IMKeyEvent* event, uint32_t modifier) {
    return event && (event->modifiers & modifier) == modifier;
}

bool im_key_event_is_keypad(const IMKeyEvent* event) {
    return event && (event->flags & IM_KEY_FLAG_KEYPAD);
}

bool im_key_event_is_dead_key(const IMKeyEvent* event) {
    return event && (event->flags & IM_KEY_FLAG_DEAD);
}

bool im_key_event_is_printable(const IMKeyEvent* event) {
    if (!event) return false;
    
    // Check if it's a regular printable ASCII character
    if (event->character >= 32 && event->character <= 126) {
        return true;
    }
    
    // Check for Unicode characters
    if (event->flags & IM_KEY_FLAG_UNICODE) {
        return true;
    }
    
    return false;
}

void im_key_event_destroy(IMKeyEvent* event) {
    im_free(event);
} 