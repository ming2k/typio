/**
 * @file test_basic_flow.c
 * @brief Integration test: basic engine end-to-end commit flow.
 *
 * @see docs/explanation/architecture-overview.md
 * @see docs/explanation/timing-model.md
 */

#include "mock_compositor.h"
#include "typio/typio.h"
#include "wayland/wl_frontend.h"
#include <cassert.h>
#include <string.h>

void test_basic_engine_commit() {
    MockCompositor *mc = mock_compositor_create();
    assert(mc);

    /* TODO: instantiate TypioWlFrontend and connect to mock compositor */
    /* TODO: send activate + key 'a' + deactivate */
    /* TODO: assert committed text == "a" */

    mock_compositor_destroy(mc);
}

int main() {
    test_basic_engine_commit();
    return 0;
}
