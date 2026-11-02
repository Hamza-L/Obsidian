#include <stddef.h>

#define INIT_POOL_SLOT_COUNT 16
#define MEM_SCALING_FACTOR 2

void * m_reserve(size_t size);
void * m_commit(void *data, size_t size);
void m_release(void *data, size_t size);
