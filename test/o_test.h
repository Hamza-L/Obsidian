#ifndef O_TEST_H
#define O_TEST_H

#include <stdio.h>

#define TEST_LOG_PATH "build/Obsidian_test.log"

extern int test_failures;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            test_failures++;                                                \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                   \
    } while (0)

void test_o_log(void);
void test_o_memory(void);
void test_o_file(void);
void test_o_window(void);

#endif
