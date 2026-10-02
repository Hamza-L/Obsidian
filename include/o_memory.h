#include <stdint.h>

typedef void* MPool;
typedef struct { uint32_t index; uint32_t age; } GHandle;

MPool memory_pool_create(const size_t slot_size);
void memory_pool_destroy(MPool pool);

GHandle memory_pool_item_add(MPool pool, void* data);
void memory_pool_item_remove(MPool pool, GHandle handle);
void* memory_pool_item_get(MPool pool, GHandle handle);

// void* array_alloc(const size_t item_size);
