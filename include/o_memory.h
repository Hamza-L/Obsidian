#ifndef O_MEMORY_H
#define O_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef void* MPool;
typedef struct { uint32_t id; uint32_t age; } MHandle;
#define HANDLE_INVALID (MHandle){0,0}
#define HANDLE_IS_NULL(handle) ((handle).id == 0)

MPool memory_pool_create(const uint32_t slot_size, const uint32_t max_count);
void memory_pool_destroy(MPool pool);

MHandle memory_pool_item_acquire(MPool pool);
void memory_pool_item_remove(MPool pool, MHandle handle);
void* memory_pool_item_get(MPool pool, MHandle handle);

bool memory_pool_is_valid(MPool pool);

// Deferred removal for items the GPU may still reference: invalidate now (the handle dies),
// recycle once the GPU is done (the slot is reused). item_at skips the age check, so it can
// still reach an invalidated slot between the two.
bool memory_pool_item_invalidate(MPool pool, MHandle handle);
bool memory_pool_item_recycle(MPool pool, MHandle handle);
void *memory_pool_item_at(MPool pool, uint32_t index);

// void* array_alloc(const size_t item_size);

#endif //O_MEMORY_H
