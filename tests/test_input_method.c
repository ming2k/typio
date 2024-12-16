#include "input_method/context.h"
#include "input_method/key_event.h"
#include "input_method/utils.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void test_context_creation() {
    IMContext* ctx = im_init(32);
    assert(ctx != NULL);
    assert(im_get_composition(ctx) != NULL);
    assert(strlen(im_get_composition(ctx)) == 0);
    assert(!ctx->is_composing);
    im_destroy(ctx);
    printf("Context creation test passed\n");
}

void test_key_event() {
    IMKeyEvent* event = im_key_event_create((IMKeyCode)'a', 'a', IM_KEY_PRESS, 0);
    assert(event != NULL);
    assert(event->code == (IMKeyCode)'a');
    assert(event->character == 'a');
    assert(event->type == IM_KEY_PRESS);
    assert(event->modifiers == 0);
    im_key_event_destroy(event);
    printf("Key event test passed\n");
}

void test_composition() {
    IMContext* ctx = im_init(32);
    IMKeyEvent* event = im_key_event_create((IMKeyCode)'a', 'a', IM_KEY_PRESS, 0);
    
    assert(im_process_key_event(ctx, event));
    assert(strcmp(im_get_composition(ctx), "a") == 0);
    assert(ctx->is_composing);
    
    im_clear_composition(ctx);
    assert(strlen(im_get_composition(ctx)) == 0);
    assert(!ctx->is_composing);
    
    im_key_event_destroy(event);
    im_destroy(ctx);
    printf("Composition test passed\n");
}

void test_utils() {
    char* str = im_strdup("Hello");
    assert(str != NULL);
    assert(strcmp(str, "Hello") == 0);
    assert(im_utf8_strlen(str) == 5);
    im_free(str);
    printf("Utils test passed\n");
}

int main() {
    printf("Running tests...\n");
    
    test_context_creation();
    test_key_event();
    test_composition();
    test_utils();
    
    printf("All tests passed!\n");
    return 0;
} 