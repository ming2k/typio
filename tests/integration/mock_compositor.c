/**
 * @file mock_compositor.c
 * @brief Minimal headless Wayland compositor for integration tests.
 */

#include "mock_compositor.h"
#include "utils/log.h"
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>

struct MockCompositor {
    struct wl_display *display;
    char socket_path[64];
    char last_preedit[256];
    char last_committed[256];
    int preedit_cursor;
};

MockCompositor *mock_compositor_create(void) {
    MockCompositor *mc = calloc(1, sizeof(*mc));
    if (!mc) return nullptr;

    mc->display = wl_display_create();
    if (!mc->display) {
        free(mc);
        return nullptr;
    }

    /* TODO: bind input_method_manager_v2, text_input_manager_v3, etc. */
    /* For now this is a skeleton that creates the display and socket. */

    const char *sock = wl_display_add_socket_auto(mc->display);
    if (!sock) {
        wl_display_destroy(mc->display);
        free(mc);
        return nullptr;
    }
    snprintf(mc->socket_path, sizeof(mc->socket_path), "wayland-%s", sock);

    return mc;
}

void mock_compositor_destroy(MockCompositor *mc) {
    if (!mc) return;
    if (mc->display) wl_display_destroy(mc->display);
    free(mc);
}

const char *mock_compositor_get_socket(MockCompositor *mc) {
    return mc ? mc->socket_path : nullptr;
}

void mock_compositor_activate(MockCompositor *mc) {
    (void)mc;
    /* TODO: send zwp_input_method_v2.activate */
}

void mock_compositor_deactivate(MockCompositor *mc) {
    (void)mc;
    /* TODO: send zwp_input_method_v2.deactivate */
}

void mock_compositor_send_key(MockCompositor *mc, uint32_t keycode, bool pressed) {
    (void)mc; (void)keycode; (void)pressed;
    /* TODO: send keyboard key event through input_method_keyboard_grab */
}

void mock_compositor_send_surrounding_text(MockCompositor *mc,
                                            const char *text,
                                            uint32_t cursor,
                                            uint32_t anchor) {
    (void)mc; (void)text; (void)cursor; (void)anchor;
    /* TODO */
}

void mock_compositor_done(MockCompositor *mc) {
    (void)mc;
    /* TODO: send zwp_input_method_v2.done */
}

const char *mock_compositor_get_last_preedit(MockCompositor *mc) {
    return mc ? mc->last_preedit : nullptr;
}

const char *mock_compositor_get_last_committed_text(MockCompositor *mc) {
    return mc ? mc->last_committed : nullptr;
}

int mock_compositor_get_preedit_cursor(MockCompositor *mc) {
    return mc ? mc->preedit_cursor : -1;
}
