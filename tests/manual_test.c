#include "input_method/context.h"
#include "input_method/key_event.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

void test_input_sequence(IMContext* ctx, const char* sequence) {
    printf("\nTesting sequence: %s\n", sequence);
    
    for (size_t i = 0; sequence[i]; i++) {
        IMKeyEvent* event = im_key_event_create((IMKeyCode)sequence[i], 
                                              sequence[i], 
                                              IM_KEY_PRESS, 
                                              IM_MOD_NONE);
        if (im_process_key_event(ctx, event)) {
            printf("Buffer: '%s', Cursor: %zu\n", 
                   im_get_composition(ctx), 
                   im_get_cursor_pos(ctx));
        }
        im_key_event_destroy(event);
    }
}

void test_special_keys(IMContext* ctx) {
    printf("\nTesting special keys:\n");

    // Test backspace
    test_input_sequence(ctx, "hello");
    IMKeyEvent* backspace = im_key_event_create(IM_KEY_BACKSPACE, 0, IM_KEY_PRESS, 0);
    im_process_key_event(ctx, backspace);
    printf("After backspace: '%s'\n", im_get_composition(ctx));
    im_key_event_destroy(backspace);

    // Test cursor movement
    IMKeyEvent* left = im_key_event_create(IM_KEY_LEFT, 0, IM_KEY_PRESS, 0);
    im_process_key_event(ctx, left);
    printf("After left: cursor at %zu\n", im_get_cursor_pos(ctx));
    im_key_event_destroy(left);

    IMKeyEvent* right = im_key_event_create(IM_KEY_RIGHT, 0, IM_KEY_PRESS, 0);
    im_process_key_event(ctx, right);
    printf("After right: cursor at %zu\n", im_get_cursor_pos(ctx));
    im_key_event_destroy(right);
}

int main() {
    IMContext* ctx = im_init(32);
    if (!ctx) {
        fprintf(stderr, "Failed to initialize input method\n");
        return 1;
    }

    // Test basic input
    test_input_sequence(ctx, "Hello World");
    im_clear_composition(ctx);

    // Test special keys
    test_special_keys(ctx);

    // Test cursor positioning
    im_clear_composition(ctx);
    test_input_sequence(ctx, "ABCDEF");
    im_set_cursor_pos(ctx, 3);
    printf("Cursor set to 3: '%s'\n", im_get_composition(ctx));

    im_destroy(ctx);
    printf("\nAll tests completed!\n");
    return 0;
} 