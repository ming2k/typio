/**
 * @file test_recent_log_dump.c
 * @brief Tests for recent log ring-buffer dumping
 */

#include "utils/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>

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

TEST(dumps_recent_logs_to_requested_file) {
    char path[] = "/tmp/typio-recent-log-XXXXXX";
    int fd = mkstemp(path);
    FILE *fp;
    char buf[4096];
    regex_t timestamp_pattern;
    int regex_ok;

    ASSERT(fd >= 0);
    close(fd);

    typio_log_set_level(TYPIO_LOG_DEBUG);
    typio_log_set_recent_dump_path(path);
    typio_log(TYPIO_LOG_INFO, "alpha");
    typio_log(TYPIO_LOG_WARNING, "beta");

    ASSERT(typio_log_dump_recent(path));

    fp = fopen(path, "r");
    ASSERT(fp != NULL);
    ASSERT(fread(buf, 1, sizeof(buf) - 1, fp) > 0);
    fclose(fp);
    buf[sizeof(buf) - 1] = '\0';

    ASSERT(strstr(buf, "alpha") != NULL);
    ASSERT(strstr(buf, "beta") != NULL);

    regex_ok = regcomp(&timestamp_pattern,
                       "\\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\] \\[(INFO|WARN)\\]",
                       REG_EXTENDED);
    ASSERT(regex_ok == 0);
    ASSERT(regexec(&timestamp_pattern, buf, 0, NULL, 0) == 0);
    regfree(&timestamp_pattern);

    unlink(path);
    typio_log_set_recent_dump_path(NULL);
}

TEST(configured_dump_writes_latest_only) {
    char dir[] = "/tmp/typio-recent-dir-XXXXXX";
    char latest[1024];
    char logs_dir[1024];

    ASSERT(mkdtemp(dir) != NULL);
    ASSERT(snprintf(latest, sizeof(latest), "%s/logs/latest.log", dir) < (int)sizeof(latest));
    ASSERT(snprintf(logs_dir, sizeof(logs_dir), "%s/logs", dir) < (int)sizeof(logs_dir));

    typio_log_set_level(TYPIO_LOG_DEBUG);
    typio_log_set_recent_dump_path(latest);
    typio_log(TYPIO_LOG_INFO, "gamma");

    ASSERT(typio_log_dump_recent_to_configured_path());
    ASSERT(access(latest, F_OK) == 0);
    ASSERT(access(logs_dir, F_OK) == 0);

    unlink(latest);
    rmdir(logs_dir);
    rmdir(dir);
    typio_log_set_recent_dump_path(NULL);
}

int main(void) {
    printf("Running recent log dump tests:\n");

    run_test_dumps_recent_logs_to_requested_file();
    run_test_configured_dump_writes_latest_only();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
