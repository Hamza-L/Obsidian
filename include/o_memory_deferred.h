#ifndef O_MEMORY_DEFERRED_H
#define O_MEMORY_DEFERRED_H
#include "o_memory.h"

bool memory_pool_item_invalidate(MPool pool, MHandle handle);
bool memory_pool_item_recycle(MPool pool, MHandle handle);
void *memory_pool_item_at(MPool pool_, uint32_t index);

#endif //O_MEMORY_DEFERRED_H
