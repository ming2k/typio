#include "input_method/key_event.h"
#include "input_method/utils.h"
#include <time.h>

IMKeyEvent* im_key_event_create(IMKeyCode code, char character, IMKeyEventType type, uint32_t modifiers) {
    IMKeyEvent* event = im_malloc(sizeof(IMKeyEvent));
    if (!event) return NULL;

    event->code = code;
    event->character = character;
    event->type = type;
    event->modifiers = modifiers;
    event->timestamp = (uint32_t)time(NULL);

    return event;
}

void im_key_event_destroy(IMKeyEvent* event) {
    im_free(event);
}

bool im_key_event_is_printable(const IMKeyEvent* event) {
    if (!event) return false;
    
    // Check if it's a regular printable ASCII character
    if (event->character >= 32 && event->character <= 126) {
        return true;
    }
    
    // Add more checks for Unicode characters if needed
    return false;
} 