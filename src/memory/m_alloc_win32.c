#include "m_alloc_internal.h"

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
    void* data = VirtualAlloc(NULL,
                              MAX_POOL_MEMORY,
                              MEM_RESERVE,
                              PAGE_READWRITE
                             );

    return VirtualAlloc(data,
                        size,
                        MEM_COMMIT,
                        PAGE_READWRITE
                       );
}

void *m_realloc(void *data, size_t size) {
    if (!data) {
        data = VirtualAlloc(NULL,
                              MAX_POOL_MEMORY,
                            MEM_RESERVE,
                            PAGE_READWRITE
                           );
    }

    return VirtualAlloc(data,
                        size,
                        MEM_COMMIT,
                        PAGE_READWRITE
                       );
}

void m_free(void *data) {
    VirtualFree(data, 0, MEM_RELEASE);
}
#endif
