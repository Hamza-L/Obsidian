#include <stddef.h>

#define INIT_POOL_SLOT_COUNT 16
// 32 gb max memory
#define MAX_POOL_MEMORY 0x800000000

void *m_malloc(size_t size);
void *m_realloc(void *data, size_t size);
void m_free(void *data);
