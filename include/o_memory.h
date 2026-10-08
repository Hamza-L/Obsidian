#include <stdint.h>
#include <stdbool.h>

typedef void* MPool;
typedef struct { uint32_t index; uint32_t age; } MHandle;
#define MHANDLE_INVALID (MHandle){0,0}

MPool memory_pool_create(const size_t slot_size);
void memory_pool_destroy(MPool pool);

MHandle memory_pool_item_acquire(MPool pool);
void memory_pool_item_remove(MPool pool, MHandle handle);
void* memory_pool_item_get(MPool pool, MHandle handle);

bool memory_pool_is_oom(MPool pool);

// void* array_alloc(const size_t item_size);
