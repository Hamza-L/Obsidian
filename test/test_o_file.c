#include "o_test.h"
#include "o_file.h"

#include <stdio.h>
#include <string.h>

static const char *k_path = "build/test_o_file.tmp";
static const char k_payload[] = "obsidian\nfile\n";

void test_o_file(void) {
    FILE *f = fopen(k_path, "wb");
    CHECK(f != NULL);
    if (!f) return;
    fwrite(k_payload, 1, sizeof k_payload - 1, f);
    fclose(f);

    uint32_t size = 0;
    read_file(k_path, NULL, &size);
    CHECK(size == sizeof k_payload - 1);

    char buffer[64] = {0};
    size = sizeof buffer;
    read_file(k_path, buffer, &size);
    CHECK(size == sizeof k_payload - 1);
    CHECK(memcmp(buffer, k_payload, size) == 0);

    size = 4;
    memset(buffer, 0, sizeof buffer);
    read_file(k_path, buffer, &size);
    CHECK(size == 4);
    CHECK(memcmp(buffer, "obsi", 4) == 0);
    CHECK(buffer[4] == 0);

    size = 123;
    read_file("build/does_not_exist.tmp", NULL, &size);
    CHECK(size == 0);

    remove(k_path);
}
