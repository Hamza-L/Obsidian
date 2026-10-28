#include <stddef.h>

#define INIT_POOL_SLOT_COUNT 16
// 1 gb max memory
#define MAX_POOL_MEMORY 0x40000000
#define MEM_SCALING_FACTOR 2

void *m_malloc(size_t size);
void *m_realloc(void *data, size_t size);
void m_free(void *data);
