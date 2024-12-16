#include "input_method/context.h"
#include "input_method/key_event.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>

// Simple function to convert keyboard input to IMKeyEvent
IMKeyEvent* create_event_from_input(int c) {
    IMKeyCode code = IM_KEY_NONE;
    char character = '\0';
    uint32_t modifiers = IM_MOD_NONE;

    switch (c) {
        case 127: // Backspace
            code = IM_KEY_BACKSPACE;
            break;
        case '\n':
            code = IM_KEY_ENTER;
            break;
        default:
            if (isprint(c)) {
                code = (IMKeyCode)c;
                character = (char)c;
            }
            break;
        case 'h':  // Left arrow (for testing)
            code = IM_KEY_LEFT;
            break;
        case 'l':  // Right arrow (for testing)
            code = IM_KEY_RIGHT;
            break;
        case 'H':  // Home
            code = IM_KEY_HOME;
            break;
        case 'L':  // End
            code = IM_KEY_END;
            break;
    }

    return im_key_event_create(code, character, IM_KEY_PRESS, modifiers);
}

void print_composition(const IMContext* ctx) {
    const char* text = im_get_composition(ctx);
    size_t cursor = im_get_cursor_pos(ctx);
    
    printf("\rComposition: ");
    for (size_t i = 0; text[i]; i++) {
        if (i == cursor) {
            printf("|");
        }
        printf("%c", text[i]);
    }
    if (cursor == strlen(text)) {
        printf("|");
    }
    printf("    \r");
    fflush(stdout);
}

int main() {
    IMContext* ctx = im_init(32);
    if (!ctx) {
        fprintf(stderr, "Failed to initialize input method\n");
        return 1;
    }

    printf("Input Method Framework Test\n");
    printf("Enter characters (press Enter to quit):\n");

    int c;
    while ((c = getchar()) != '\n') {
        IMKeyEvent* event = create_event_from_input(c);
        if (event) {
            if (im_process_key_event(ctx, event)) {
                print_composition(ctx);
            }
            im_key_event_destroy(event);
        }
    }

    printf("\nFinal composition: %s\n", im_get_composition(ctx));
    im_destroy(ctx);
    return 0;
}