#ifndef INPUT_METHOD_KEY_EVENT_H
#define INPUT_METHOD_KEY_EVENT_H

#include <stdint.h>
#include <stdbool.h>

// Key modifiers
#define IM_MOD_NONE   0x0000
#define IM_MOD_SHIFT  0x0001
#define IM_MOD_CTRL   0x0002
#define IM_MOD_ALT    0x0004
#define IM_MOD_CAPS   0x0008

// Special keys
typedef enum {
    IM_KEY_NONE = 0,
    IM_KEY_BACKSPACE = 0x08,
    IM_KEY_TAB = 0x09,
    IM_KEY_ENTER = 0x0D,
    IM_KEY_ESC = 0x1B,
    IM_KEY_SPACE = 0x20,
    IM_KEY_DELETE = 0x7F,
    
    // Function keys
    IM_KEY_F1 = 0x100,
    IM_KEY_F2,
    IM_KEY_F3,
    IM_KEY_F4,
    
    // Navigation keys
    IM_KEY_LEFT,
    IM_KEY_RIGHT,
    IM_KEY_UP,
    IM_KEY_DOWN,
    IM_KEY_HOME,
    IM_KEY_END,
    IM_KEY_PAGE_UP,
    IM_KEY_PAGE_DOWN
} IMKeyCode;

// Key event types
typedef enum {
    IM_KEY_PRESS,
    IM_KEY_RELEASE,
    IM_KEY_REPEAT
} IMKeyEventType;

// Key event structure
typedef struct {
    IMKeyCode code;           // Key code
    char character;           // ASCII character (if printable)
    IMKeyEventType type;      // Event type
    uint32_t modifiers;       // Key modifiers
    uint32_t timestamp;       // Event timestamp
} IMKeyEvent;

// Create a key event
IMKeyEvent* im_key_event_create(IMKeyCode code, char character, IMKeyEventType type, uint32_t modifiers);

// Destroy a key event
void im_key_event_destroy(IMKeyEvent* event);

// Check if a key event represents a printable character
bool im_key_event_is_printable(const IMKeyEvent* event);

#endif // INPUT_METHOD_KEY_EVENT_H 