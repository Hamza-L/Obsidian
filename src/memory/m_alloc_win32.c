#include "m_alloc_internal.h"
#include "o_log.h"

#include <Windows.h>

#if 0
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

void *m_malloc(size_t size) {
    void *data = VirtualAlloc(NULL, MAX_POOL_MEMORY, MEM_RESERVE, PAGE_READWRITE);
    if (!data) {
        LOG_ERROR("VirtualAlloc reserve of %zu bytes failed: %lu", (size_t)MAX_POOL_MEMORY, GetLastError());
        return NULL;
    }
    if (!VirtualAlloc(data, size, MEM_COMMIT, PAGE_READWRITE)) {
        LOG_ERROR("VirtualAlloc commit of %zu bytes failed: %lu", size, GetLastError());
        VirtualFree(data, 0, MEM_RELEASE);
        return NULL;
    }
    return data;
}

void *m_realloc(void *data, size_t size) {
    if (!data) return m_malloc(size);
    if (!VirtualAlloc(data, size, MEM_COMMIT, PAGE_READWRITE)) {
        LOG_ERROR("VirtualAlloc commit of %zu bytes failed: %lu", size, GetLastError());
        return NULL;
    }
    return data;
}

void m_free(void *data) {
    VirtualFree(data, 0, MEM_RELEASE);
}
#endif
