#ifndef O_ALLOC_H
#define O_ALLOC_H

#include <stddef.h>

void * m_reserve(size_t size);
void * m_commit(void *data, size_t size);
void m_release(void *data, size_t size);

#endif //O_ALLOC_H
