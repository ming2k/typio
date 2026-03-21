/**
 * @file test_identity.c
 * @brief Tests for focused-app identity provider and engine memory
 */

#include "typio/instance.h"
#include "typio/config.h"
#include "utils/string.h"
#include "wayland/identity.h"
#include "wayland/wl_frontend_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  Running %s... ", #name); \
        tests_run++; \
        test_##name(); \
        tests_passed++; \
        printf("OK\n"); \
    } \
    static void test_##name(void)

#define ASSERT(expr) \
    do { \
        if (!(expr)) { \
            printf("FAILED\n"); \
            printf("    Assertion failed: %s\n", #expr); \
            printf("    At %s:%d\n", __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

static char *hex_encode(const char *text) {
    static const char digits[] = "0123456789abcdef";
    size_t len = strlen(text);
    char *encoded = calloc(len * 2 + 1, sizeof(char));

    ASSERT(encoded != NULL);
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)text[i];
        encoded[i * 2] = digits[(ch >> 4) & 0x0f];
        encoded[i * 2 + 1] = digits[ch & 0x0f];
    }

    return encoded;
}

TEST(parses_niri_focused_window_app_id) {
    TypioWlIdentity identity = {};
    ASSERT(typio_wl_identity_parse_niri_focused_window(
        "{\"Ok\":{\"FocusedWindow\":{\"id\":12,\"app_id\":\"Alacritty\"}}}\n",
        &identity));
    ASSERT(strcmp(identity.provider_name, "niri") == 0);
    ASSERT(strcmp(identity.app_id, "Alacritty") == 0);
    ASSERT(strcmp(identity.stable_key, "niri:Alacritty") == 0);

    typio_wl_identity_clear(&identity);
}

TEST(remember_active_engine_writes_identity_mapping) {
    char state_dir[] = "/tmp/typio-identity-memory-XXXXXX";
    char *state_path;
    char *encoded_key;
    char *full_key;
    TypioInstanceConfig config = {};
    TypioInstance *instance;
    TypioWlFrontend frontend = {};
    TypioConfig *saved;

    ASSERT(mkdtemp(state_dir) != NULL);
    config.state_dir = state_dir;
    instance = typio_instance_new_with_config(&config);
    ASSERT(instance != NULL);

    frontend.instance = instance;
    frontend.current_identity.provider_name = typio_strdup("niri");
    frontend.current_identity.app_id = typio_strdup("org.example.Editor");
    frontend.current_identity.stable_key = typio_strdup("niri:org.example.Editor");

    typio_wl_frontend_remember_active_engine(&frontend, "rime");

    state_path = typio_path_join(state_dir, "identity-engine-state.toml");
    ASSERT(state_path != NULL);
    saved = typio_config_load_file(state_path);
    ASSERT(saved != NULL);

    encoded_key = hex_encode("niri:org.example.Editor");
    full_key = typio_strjoin3("identities.", encoded_key, "");
    ASSERT(full_key != NULL);
    ASSERT(strcmp(typio_config_get_string(saved, full_key, ""), "rime") == 0);

    free(state_path);
    free(encoded_key);
    free(full_key);
    typio_config_free(saved);
    typio_wl_identity_clear(&frontend.current_identity);
    typio_instance_free(instance);
}

int main(void) {
    printf("Running identity tests:\n");

    run_test_parses_niri_focused_window_app_id();
    run_test_remember_active_engine_writes_identity_mapping();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
