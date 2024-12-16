#include "input_method/context.h"
#include "input_method/key_event.h"
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

// Set terminal to raw mode
static struct termios orig_termios;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void print_key_event(const IMKeyEvent* event) {
    printf("\rKey Event: code=0x%04x char='%c' mod=0x%04x state=%d flags=0x%04x scan=0x%04x  \n",
           event->code,
           event->character ? event->character : ' ',
           event->modifiers,
           event->state,
           event->flags,
           event->scancode);
}

int main() {
    enable_raw_mode();
    
    printf("Keyboard Event Capture Test (Press 'q' to quit)\n");
    
    IMContext* ctx = im_init(32);
    if (!ctx) {
        fprintf(stderr, "Failed to initialize input method\n");
        return 1;
    }

    while (1) {
        char c = getchar();
        if (c == 'q') break;

        // Create key event with more details
        uint32_t mods = IM_MOD_NONE;
        if (c >= 'A' && c <= 'Z') mods |= IM_MOD_SHIFT;
        
        IMKeyEvent* event = im_key_event_create_ex(
            (IMKeyCode)c,
            c,
            IM_KEY_PRESS,
            mods,
            IM_KEY_STATE_DOWN,
            IM_KEY_FLAG_NONE,
            c  // Using char as scancode for demonstration
        );

        if (event) {
            print_key_event(event);
            if (im_process_key_event(ctx, event)) {
                printf("Composition: %s\n", im_get_composition(ctx));
            }
            im_key_event_destroy(event);
        }
    }

    im_destroy(ctx);
    return 0;
} 