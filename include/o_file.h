#ifndef O_FILE_H
#define O_FILE_H

#include <string.h>
#include <stdint.h>
#include <stdio.h>

typedef uint32_t OFile; // value of 0 is invalid

void read_file(const char* filepath, void* data, uint32_t *size);

#endif //O_FILE_H
