#include "m_alloc_internal.h"

#if 0
#include <stdlib.h>
void *m_malloc(size_t size) {
    return malloc(size);
}

void *m_realloc(void *data, size_t size) {
    return realloc(data, size);
}

void m_free(void *data) {
    free(data);
}

#else
#include "o_log.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>

void *m_malloc(size_t size) {
    void *data = mmap(NULL,
                      MAX_POOL_MEMORY,
                      PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1,
                      0);

    if (data == MAP_FAILED) {
        LOG_ERROR("mmap reserve of %zu bytes failed: %s", (size_t)MAX_POOL_MEMORY, strerror(errno));
        return NULL;
    }

    if (mprotect(data, size, PROT_READ | PROT_WRITE) != 0) {
        LOG_ERROR("mprotect of %zu bytes failed: %s", size, strerror(errno));
        munmap(data, MAX_POOL_MEMORY);
        return NULL;
    }

    return data;
}

void *m_realloc(void *data, size_t size) {
    if (!data) {
        return m_malloc(size);
    } else {
        if (mprotect(data, size, PROT_READ | PROT_WRITE) != 0) {
            LOG_ERROR("mprotect of %zu bytes failed: %s", size, strerror(errno));
            return NULL;
        }
        return data;
    }
}

void m_free(void *data) {
    munmap(data, MAX_POOL_MEMORY);
}
#endif
