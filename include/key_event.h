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
#define IM_MOD_META   0x0010  // Windows/Command key
#define IM_MOD_NUM    0x0020  // Num Lock
#define IM_MOD_SCROLL 0x0040  // Scroll Lock

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

// Key states
typedef enum {
    IM_KEY_STATE_UP,
    IM_KEY_STATE_DOWN,
    IM_KEY_STATE_REPEAT
} IMKeyState;

// Key event flags
#define IM_KEY_FLAG_NONE    0x0000
#define IM_KEY_FLAG_UNICODE 0x0001  // Event contains Unicode character
#define IM_KEY_FLAG_DEAD    0x0002  // Dead key (accent, etc.)
#define IM_KEY_FLAG_KEYPAD  0x0004  // Keypad key

// Key event structure
typedef struct {
    IMKeyCode code;           // Key code
    char character;           // ASCII character (if printable)
    IMKeyEventType type;      // Event type
    uint32_t modifiers;       // Key modifiers
    uint32_t timestamp;       // Event timestamp (milliseconds)
    IMKeyState state;         // Key state
    uint32_t flags;           // Event flags
    uint32_t scancode;        // Hardware scancode
    uint32_t repeat_count;    // Key repeat count
} IMKeyEvent;

// Create a key event
IMKeyEvent* im_key_event_create(IMKeyCode code, char character, IMKeyEventType type, uint32_t modifiers);

// Destroy a key event
void im_key_event_destroy(IMKeyEvent* event);

// Check if a key event represents a printable character
bool im_key_event_is_printable(const IMKeyEvent* event);

// Create key event with detailed information
IMKeyEvent* im_key_event_create_ex(
    IMKeyCode code,
    char character,
    IMKeyEventType type,
    uint32_t modifiers,
    IMKeyState state,
    uint32_t flags,
    uint32_t scancode
);

// Get modifier key state
bool im_key_event_has_modifier(const IMKeyEvent* event, uint32_t modifier);

// Check if event is from keypad
bool im_key_event_is_keypad(const IMKeyEvent* event);

// Check if event is a dead key
bool im_key_event_is_dead_key(const IMKeyEvent* event);

#endif // INPUT_METHOD_KEY_EVENT_H 