#include <string.h>
#include <stdint.h>
#include <stdio.h>

typedef uint32_t OFile; // value of 0 is invalid

void read_file(const char* filepath, void* data, uint32_t *size);
