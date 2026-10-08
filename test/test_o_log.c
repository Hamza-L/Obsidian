#include "o_test.h"
#include "o_log.h"

#include <stdio.h>
#include <string.h>

static const char *k_path = "build/test_o_log.tmp";

static size_t read_all(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = 0;
    return n;
}

static int count_lines(const char *s) {
    int n = 0;
    for (; *s; s++) n += *s == '\n';
    return n;
}

void test_o_log(void) {
    CHECK(!log_open_file("build/no/such/dir/log.txt"));
    CHECK(log_open_file(k_path));
    log_set_sinks(O_LOG_SINK_FILE);
    log_set_level(O_LOG_INFO);

    LOG_DEBUG("filtered %d", 1);
    LOG_INFO("info %s %d", "message", 42);
    LOG_WARN("warn");
    LOG_ERROR("error");

    char huge[2048];
    memset(huge, 'x', sizeof huge - 1);
    huge[sizeof huge - 1] = 0;
    LOG_ERROR("%s", huge);

    log_set_level(O_LOG_DEBUG);
    LOG_DEBUG("now visible");
    log_close_file();

    char content[8192];
    CHECK(read_all(k_path, content, sizeof content) > 0);
    CHECK(count_lines(content) == 5);
    CHECK(strstr(content, "filtered") == NULL);
    CHECK(strstr(content, "INFO  test_o_log.c:") != NULL);
    CHECK(strstr(content, "info message 42\n") != NULL);
    CHECK(strstr(content, "WARN  test_o_log.c:") != NULL);
    CHECK(strstr(content, "ERROR test_o_log.c:") != NULL);
    CHECK(strstr(content, "xxx...\n") != NULL);
    CHECK(strstr(content, "DEBUG test_o_log.c:") != NULL);
    CHECK(strstr(content, "src/") == NULL && strstr(content, "test/") == NULL);

    const char *longest = strstr(content, "xxx");
    CHECK(longest && strchr(longest, '\n') - strstr(content, "ERROR test_o_log.c:") < 1100);

    remove(k_path);
    log_set_level(O_LOG_INFO);
    log_open_file(TEST_LOG_PATH);
    log_set_sinks(O_LOG_SINK_FILE);
}
