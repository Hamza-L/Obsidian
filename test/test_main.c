#include "o_log.h"
#include "o_test.h"

int test_failures;

typedef struct {
    const char *name;
    void (*run)(void);
} Module;

int main(void) {
    const Module modules[] = {
        {"o_log", test_o_log},
        {"o_memory", test_o_memory},
        {"o_file", test_o_file},
        {"o_window", test_o_window},
    };
    const int module_count = sizeof modules / sizeof modules[0];
    int modules_failed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    log_open_file(TEST_LOG_PATH);
    log_set_sinks(O_LOG_SINK_FILE);
    for (int i = 0; i < module_count; i++) {
        const int before = test_failures;
        printf("%s\n", modules[i].name);
        modules[i].run();
        if (test_failures == before) {
            printf("  ok\n");
        } else {
            modules_failed++;
        }
    }

    log_close_file();
    printf("\n%d/%d modules passed, %d check(s) failed  (module log: %s)\n", module_count - modules_failed, module_count, test_failures, TEST_LOG_PATH);
    return test_failures != 0;
}
