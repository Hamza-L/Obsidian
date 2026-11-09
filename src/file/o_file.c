#include "o_file.h"
#include "o_log.h"

#include "core/o_typedefs.h"

void read_file(const char *filepath, void *data, u32 *size) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        LOG_WARN("cannot open %s", filepath);
        *size = 0;
        return;
    }
    if (data) {
        *size = (u32)fread(data, 1, *size, f);
    } else {
        fseek(f, 0, SEEK_END);
        *size = (u32)ftell(f);
    }
    fclose(f);
}
