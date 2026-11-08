#include "memory/o_memory_internal.h"

#include "o_log.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>

void *m_reserve(size_t size) {
    void *data = mmap(NULL,
                      size,
                      PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1,
                      0);

    if (data == MAP_FAILED || !data) {
        LOG_ERROR("mmap reserve of %zu bytes failed: %s", size, strerror(errno));
        return NULL;
    }
    return data;
}

void *m_commit(void *data, size_t size) {
    if (data) {
        if (mprotect(data, size, PROT_READ | PROT_WRITE) != 0) {
            LOG_ERROR("mprotect of %zu bytes failed: %s", size, strerror(errno));
            return NULL;
        }
        return data;
    }
    LOG_ERROR("attempting to commit unreserved memory");
    return NULL;
}

void m_release(void *data, size_t size) {
    if (!data) {
        LOG_ERROR("attempting to release unreserved memory");
        return;
    }

    munmap(data, size);
}
