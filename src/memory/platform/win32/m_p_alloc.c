#include "memory/m_alloc.h"

#include "o_log.h"

#include <Windows.h>

void *m_reserve(size_t size) {
    void *data = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);

    if (!data) {
        LOG_ERROR("VirtualAlloc reserve of %zu bytes failed: %lu", size, GetLastError());
        return NULL;
    }
    return data;
}

void *m_commit(void *data, size_t size) {
    if (data) {
        if (!VirtualAlloc(data, size, MEM_COMMIT, PAGE_READWRITE)) {
            LOG_ERROR("VirtualAlloc commit of %zu bytes failed: %lu", size, GetLastError());
            return NULL;
        }
        return data;
    }
    LOG_ERROR("attempting to commit unreserved memory");
    return NULL;
}

void m_release(void *data, size_t size) {
    (void)size;
    if (!data) {
        LOG_ERROR("attempting to release unreserved memory");
        return;
    }

    VirtualFree(data, 0, MEM_RELEASE);
}
