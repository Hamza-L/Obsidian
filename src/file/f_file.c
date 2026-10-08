#include "o_file.h"
#include "o_log.h"

void read_file(const char *filepath, void *data, uint32_t *size) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        LOG_WARN("cannot open %s", filepath);
        *size = 0;
        return;
    }
    if (data) {
        *size = (uint32_t)fread(data, 1, *size, f);
    } else {
        fseek(f, 0, SEEK_END);
        *size = (uint32_t)ftell(f);
    }
    fclose(f);
}
