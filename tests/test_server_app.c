#include "server_app.h"

#include "typio/typio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)

typedef struct FakeTray {
    char icon[128];
    char engine[128];
    bool active;
} FakeTray;

static void ensure_dir(const char *path) {
    ASSERT(path != NULL);
    ASSERT(mkdir(path, 0755) == 0 || access(path, F_OK) == 0);
}

static TypioInstance *create_temp_instance(char *root_template,
                                           TypioInstanceConfig *config_out) {
    static char config_dir[1024];
    static char data_dir[1024];
    static char state_dir[1024];
    static char engine_dir[1024];

    ASSERT(mkdtemp(root_template) != NULL);
    ASSERT(snprintf(config_dir, sizeof(config_dir), "%s/config", root_template) < (int)sizeof(config_dir));
    ASSERT(snprintf(data_dir, sizeof(data_dir), "%s/data", root_template) < (int)sizeof(data_dir));
    ASSERT(snprintf(state_dir, sizeof(state_dir), "%s/state", root_template) < (int)sizeof(state_dir));
    ASSERT(snprintf(engine_dir, sizeof(engine_dir), "%s/engines", root_template) < (int)sizeof(engine_dir));

    ensure_dir(config_dir);
    ensure_dir(data_dir);
    ensure_dir(state_dir);
    ensure_dir(engine_dir);

    memset(config_out, 0, sizeof(*config_out));
    config_out->config_dir = config_dir;
    config_out->data_dir = data_dir;
    config_out->state_dir = state_dir;
    config_out->engine_dir = engine_dir;
    return typio_instance_new_with_config(config_out);
}

static TypioResult mock_init(TypioEngine *engine, [[maybe_unused]] TypioInstance *instance) {
    engine->active = true;
    return TYPIO_OK;
}

static TypioKeyProcessResult mock_process_key([[maybe_unused]] TypioEngine *engine,
                                              [[maybe_unused]] TypioInputContext *ctx,
                                              [[maybe_unused]] const TypioKeyEvent *event) {
    return TYPIO_KEY_NOT_HANDLED;
}

static const TypioEngineInfo mock_rime_info = {
    .name = "rime",
    .display_name = "Rime",
    .description = "Mock rime engine",
    .version = "1.0",
    .author = "test",
    .icon = "typio-rime",
    .language = "zh_CN",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
    .capabilities = TYPIO_CAP_PREEDIT,
    .api_version = TYPIO_API_VERSION,
};

static const TypioEngineOps mock_rime_ops = {
    .init = mock_init,
    .destroy = NULL,
    .process_key = mock_process_key,
};

static const TypioEngineInfo *mock_rime_get_info(void) {
    return &mock_rime_info;
}

static TypioEngine *mock_rime_create(void) {
    return typio_engine_new(&mock_rime_info, &mock_rime_ops);
}

TEST(engine_change_preserves_dynamic_status_icon_for_tray) {
    char root[] = "/tmp/typio-server-app-test-XXXXXX";
    TypioInstanceConfig config = {};
    TypioInstance *instance = create_temp_instance(root, &config);
    TypioEngineManager *manager;
    TypioServerApp app = {};
    FakeTray tray = {};

    ASSERT(instance != NULL);
    ASSERT(typio_instance_init(instance) == TYPIO_OK);

    manager = typio_instance_get_engine_manager(instance);
    ASSERT(manager != NULL);
    ASSERT(typio_engine_manager_register(manager,
                                         mock_rime_create,
                                         mock_rime_get_info) == TYPIO_OK);
    ASSERT(typio_engine_manager_set_active(manager, "rime") == TYPIO_OK);

    app.instance = instance;
    app.tray = (TypioTray *)&tray;

    typio_instance_notify_status_icon(instance, "typio-rime-latin");
    typio_server_test_on_engine_change(instance, &mock_rime_info, &app);

    ASSERT_STR_EQ(tray.icon, "typio-rime-latin");
    ASSERT_STR_EQ(tray.engine, "rime");
    ASSERT(tray.active);

    typio_instance_free(instance);
}

TEST(engine_change_uses_static_icon_after_dynamic_engine) {
    char root[] = "/tmp/typio-server-app-test-XXXXXX";
    TypioInstanceConfig config = {};
    TypioInstance *instance = create_temp_instance(root, &config);
    TypioEngineManager *manager;
    TypioServerApp app = {};
    FakeTray tray = {};

    ASSERT(instance != NULL);
    ASSERT(typio_instance_init(instance) == TYPIO_OK);

    manager = typio_instance_get_engine_manager(instance);
    ASSERT(manager != NULL);
    ASSERT(typio_engine_manager_register(manager,
                                         mock_rime_create,
                                         mock_rime_get_info) == TYPIO_OK);
    ASSERT(typio_engine_manager_set_active(manager, "rime") == TYPIO_OK);

    app.instance = instance;
    app.tray = (TypioTray *)&tray;

    typio_instance_notify_status_icon(instance, "typio-rime-latin");
    ASSERT(typio_engine_manager_set_active(manager, "basic") == TYPIO_OK);
    typio_server_test_on_engine_change(instance, NULL, &app);

    ASSERT_STR_EQ(tray.icon, "typio-keyboard");
    ASSERT_STR_EQ(tray.engine, "basic");
    ASSERT(tray.active);

    typio_instance_free(instance);
}

int main(void) {
    printf("Running server app tests:\n");
    run_test_engine_change_preserves_dynamic_status_icon_for_tray();
    run_test_engine_change_uses_static_icon_after_dynamic_engine();
    printf("\nPassed %d/%d tests\n", tests_passed, tests_run);
    return 0;
}

#ifdef HAVE_SYSTRAY
TypioTray *typio_tray_new([[maybe_unused]] TypioInstance *instance,
                          [[maybe_unused]] const TypioTrayConfig *config) {
    return NULL;
}

void typio_tray_destroy([[maybe_unused]] TypioTray *tray) {}
int typio_tray_get_fd([[maybe_unused]] TypioTray *tray) { return -1; }
int typio_tray_dispatch([[maybe_unused]] TypioTray *tray) { return 0; }
void typio_tray_set_status([[maybe_unused]] TypioTray *tray,
                           [[maybe_unused]] TypioTrayStatus status) {}
void typio_tray_set_tooltip([[maybe_unused]] TypioTray *tray,
                            [[maybe_unused]] const char *title,
                            [[maybe_unused]] const char *description) {}
bool typio_tray_is_registered([[maybe_unused]] TypioTray *tray) { return false; }

void typio_tray_set_icon(TypioTray *tray, const char *icon_name) {
    FakeTray *fake = (FakeTray *)tray;
    snprintf(fake->icon, sizeof(fake->icon), "%s", icon_name ? icon_name : "");
}

void typio_tray_update_engine(TypioTray *tray, const char *engine_name, bool is_active) {
    FakeTray *fake = (FakeTray *)tray;
    snprintf(fake->engine, sizeof(fake->engine), "%s", engine_name ? engine_name : "");
    fake->active = is_active;
}
#endif

#ifdef HAVE_STATUS_BUS
TypioStatusBus *typio_status_bus_new([[maybe_unused]] TypioInstance *instance) { return NULL; }
void typio_status_bus_destroy([[maybe_unused]] TypioStatusBus *bus) {}
void typio_status_bus_emit_properties_changed([[maybe_unused]] TypioStatusBus *bus) {}
void typio_status_bus_set_stop_callback([[maybe_unused]] TypioStatusBus *bus,
                                        [[maybe_unused]] TypioStatusBusStopCallback callback,
                                        [[maybe_unused]] void *user_data) {}
#endif

#ifdef HAVE_WAYLAND
TypioWlFrontend *typio_wl_frontend_new([[maybe_unused]] TypioInstance *instance,
                                       [[maybe_unused]] const TypioWlFrontendConfig *config) {
    return NULL;
}
void typio_wl_frontend_destroy([[maybe_unused]] TypioWlFrontend *frontend) {}
int typio_wl_frontend_run([[maybe_unused]] TypioWlFrontend *frontend) { return 0; }
void typio_wl_frontend_stop([[maybe_unused]] TypioWlFrontend *frontend) {}
const char *typio_wl_frontend_get_error([[maybe_unused]] TypioWlFrontend *frontend) { return NULL; }
void typio_wl_frontend_set_tray([[maybe_unused]] TypioWlFrontend *frontend,
                                [[maybe_unused]] void *tray) {}
void typio_wl_frontend_set_status_bus([[maybe_unused]] TypioWlFrontend *frontend,
                                      [[maybe_unused]] void *status_bus) {}
void typio_wl_frontend_remember_active_engine([[maybe_unused]] TypioWlFrontend *frontend,
                                              [[maybe_unused]] const char *engine_name) {}
#endif
