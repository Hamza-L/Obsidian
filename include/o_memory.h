#include <stdint.h>

typedef void* MPool;
typedef struct { uint32_t index; uint32_t age; } MHandle;

MPool memory_pool_create(const size_t slot_size);
void memory_pool_destroy(MPool pool);

MHandle memory_pool_item_add(MPool pool, void* data, size_t size);
void memory_pool_item_remove(MPool pool, MHandle handle);
void* memory_pool_item_get(MPool pool, MHandle handle);

// void* array_alloc(const size_t item_size);
